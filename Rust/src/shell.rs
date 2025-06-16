use std::collections::HashMap;
use std::fs::{File, OpenOptions};
use std::process::{Child, Command, Stdio};

use crate::builtin;
use crate::parser::tokenize;
use crate::utils::{redirect_stdio, Job};

pub struct Shell {
    pub history: Vec<String>,
    pub jobs: Vec<Job>,
    pub vars: HashMap<String, String>,
    pub aliases: HashMap<String, String>,
    pub next_job_id: usize,
}

impl Shell {
    pub fn new() -> Self {
        Self {
            history: Vec::new(),
            jobs: Vec::new(),
            vars: HashMap::new(),
            aliases: HashMap::new(),
            next_job_id: 1,
        }
    }

    pub fn load_history(&mut self) {
        if let Ok(file) = File::open(".rust_shell_history") {
            for line in std::io::BufReader::new(file).lines().flatten() {
                self.history.push(line);
            }
        }
    }

    pub fn save_history(&self) {
        if let Ok(mut file) = OpenOptions::new()
            .create(true)
            .write(true)
            .truncate(true)
            .open(".rust_shell_history")
        {
            for line in &self.history {
                writeln!(file, "{}", line).ok();
            }
        }
    }

    pub fn handle_builtin(&mut self, input: &str) -> bool {
        if input == "exit" {
            self.save_history();
            println!("Bye!");
            std::process::exit(0);
        } else if input == "jobs" {
            self.list_jobs();
            return true;
        } else if input.starts_with("fg ") {
            self.bring_job_foreground(input);
            return true;
        } else if input.starts_with("bg ") {
            self.resume_job_background(input);
            return true;
        } else if input.starts_with("cd ") {
            builtin::handle_cd(input);
            return true;
        }
        false
    }

    pub fn run_command(&mut self, input: &str) -> Result<(), Box<dyn std::error::Error>> {
        if input.contains(';') {
            for cmd in input.split(';') {
                self.run_command(cmd.trim())?;
            }
            return Ok(());
        }

        if input.ends_with('&') {
            let cmd_str = input.trim_end_matches('&').trim();
            let tokens = tokenize(cmd_str);
            if tokens.is_empty() {
                return Ok(());
            }
            let mut cmd = Command::new(&tokens[0]);
            cmd.args(&tokens[1..]);
            let child = cmd.spawn()?;
            let job = Job {
                id: self.next_job_id,
                command: cmd_str.to_string(),
                process: child,
            };
            println!("[{}] {}", job.id, job.command);
            self.jobs.push(job);
            self.next_job_id += 1;
            return Ok(());
        }

        self.run_single_command(input)
    }

    pub fn run_single_command(&self, input: &str) -> Result<(), Box<dyn std::error::Error>> {
        let tokens = tokenize(input);
        if tokens.is_empty() {
            return Ok(());
        }

        let (program, args, stdin, stdout, append) = redirect_stdio(&tokens)?;
        let mut cmd = Command::new(program);
        cmd.args(args);

        if let Some(file) = stdin {
            cmd.stdin(Stdio::from(file));
        }
        if let Some(file) = stdout {
            cmd.stdout(Stdio::from(file));
        }

        let mut child = cmd.spawn()?;
        child.wait()?;
        Ok(())
    }

    pub fn clean_up_finished_jobs(&mut self) {
        self.jobs
            .retain(|j| j.process.try_wait().unwrap().is_none());
    }

    pub fn list_jobs(&mut self) {
        self.clean_up_finished_jobs();
        for job in &self.jobs {
            println!("[{}] Running: {}", job.id, job.command);
        }
    }

    pub fn bring_job_foreground(&mut self, input: &str) {
        let id = input
            .trim()
            .split_whitespace()
            .nth(1)
            .and_then(|s| s.parse::<usize>().ok());
        if let Some(id) = id {
            if let Some(pos) = self.jobs.iter().position(|j| j.id == id) {
                let mut job = self.jobs.remove(pos);
                println!("Bringing job [{}] to foreground: {}", job.id, job.command);
                job.process.wait().ok();
            } else {
                println!("No such job ID.");
            }
        } else {
            println!("Usage: fg <job_id>");
        }
    }

    pub fn resume_job_background(&mut self, input: &str) {
        let id = input
            .trim()
            .split_whitespace()
            .nth(1)
            .and_then(|s| s.parse::<usize>().ok());
        if let Some(id) = id {
            if self.jobs.iter().any(|j| j.id == id) {
                println!("Resumed job [{}] in background (simulated).", id);
            } else {
                println!("No such job ID.");
            }
        } else {
            println!("Usage: bg <job_id>");
        }
    }
}
