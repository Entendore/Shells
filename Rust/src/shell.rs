use std::collections::HashMap;
use std::fs::{File, OpenOptions};
use std::process::{Child, Command, Stdio};
use std::time::Instant;

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
        } else if input.starts_with("kill ") {
            let id = input
                .split_whitespace()
                .nth(1)
                .and_then(|s| s.parse::<usize>().ok());
            if let Some(id) = id {
                self.kill_job(id);
            } else {
                println!("Usage: kill <job_id>");
            }
            return true;
        }
        false
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

    pub fn time_command(&mut self, input: &str) -> Result<(), Box<dyn std::error::Error>> {
        let command = input.trim_start_matches("time ").trim();
        let start = Instant::now();
        self.run_command(command)?;
        let duration = start.elapsed();
        println!("Command executed in: {:.2?}", duration);
        Ok(())
    }

    pub fn handle_alias(&mut self, input: &str) -> bool {
        if input.starts_with("alias ") {
            let rest = input.trim_start_matches("alias ").trim();
            if rest.is_empty() {
                for (k, v) in &self.aliases {
                    println!("alias {}='{}'", k, v);
                }
                return true;
            }
            if let Some(pos) = rest.find('=') {
                let key = &rest[..pos];
                let val = rest[pos + 1..].trim_matches('\'').trim_matches('"');
                self.aliases.insert(key.to_string(), val.to_string());
                return true;
            }
        } else if input.starts_with("unalias ") {
            let key = input.trim_start_matches("unalias ").trim();
            self.aliases.remove(key);
            return true;
        }
        false
    }

    pub fn set_variable(&mut self, input: &str) -> bool {
        if let Some(eq_pos) = input.find('=') {
            let (var, val) = input.split_at(eq_pos);
            if !var.trim().is_empty() && !val[1..].trim().is_empty() && !var.contains(' ') {
                self.vars.insert(var.to_string(), val[1..].to_string());
                return true;
            }
        }
        false
    }

    fn expand_variables_in_tokens(&self, tokens: &[String]) -> Vec<String> {
        tokens
            .iter()
            .map(|token| {
                if token.starts_with('$') {
                    let var_name = &token[1..];
                    self.vars
                        .get(var_name)
                        .cloned()
                        .unwrap_or_else(|| "".to_string())
                } else {
                    token.clone()
                }
            })
            .collect()
    }

    pub fn kill_job(&mut self, id: usize) {
        if let Some(pos) = self.jobs.iter().position(|j| j.id == id) {
            let job = &self.jobs[pos];
            if let Err(e) = job.process.kill() {
                println!("Failed to kill job [{}]: {}", id, e);
            } else {
                println!("Job [{}] killed", id);
                self.jobs.remove(pos);
            }
        } else {
            println!("No such job ID.");
        }
    }

    pub fn run_command(&mut self, input: &str) -> Result<(), Box<dyn std::error::Error>> {
        if self.set_variable(input) {
            return Ok(());
        }
        let mut tokens = vec![];
        let mut last_op = None; // Some(true) for &&, Some(false) for ||, None for first

        for part in input.split("&&") {
            if last_op.is_some() {
                // previous was &&
                let success = self.run_command(part.trim()).is_ok();
                if !success {
                    return Ok(());
                }
            } else {
                self.run_command(part.trim())?;
            }
            last_op = Some(true);
        }

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

        let tokens = self.expand_variables_in_tokens(&tokens);

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

    pub fn log_command(&self, input: &str) {
        if let Ok(mut file) = OpenOptions::new()
            .create(true)
            .append(true)
            .open(".rust_shell_command_log")
        {
            let time = Local::now().format("%Y-%m-%d %H:%M:%S");
            writeln!(file, "[{}] {}", time, input).ok();
        }
    }

    pub fn list_jobs(&mut self) {
        self.clean_up_finished_jobs();
        for job in &self.jobs {
            println!("[{}] Running: {}", job.id, job.command);
        }
    }

    pub fn handle_cd(input: &str) {
        let mut args = input.split_whitespace();
        args.next(); // skip "cd"
        let target = args.next().unwrap_or("");
        if target == "-" {
            if let Ok(oldpwd) = env::var("OLDPWD") {
                println!("{}", oldpwd);
                env::set_current_dir(oldpwd).unwrap_or_else(|e| println!("cd: {}", e));
            } else {
                println!("cd: OLDPWD not set");
            }
        } else if !target.is_empty() {
            if let Ok(current) = env::current_dir() {
                env::set_var("OLDPWD", current);
            }
            if let Err(e) = env::set_current_dir(target) {
                println!("cd: {}", e);
            }
        } else {
            let home = env::var("HOME").unwrap_or_else(|_| "/".to_string());
            env::set_current_dir(home).unwrap_or_else(|e| println!("cd: {}", e));
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
