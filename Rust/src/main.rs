mod builtin;
mod parser;
mod shell;
mod utils;

use shell::Shell;
use std::io::{self, Write};

fn main() {
    let mut shell = Shell::new();
    shell.load_history();

    loop {
        print!("$ ");
        io::stdout().flush().unwrap();

        let mut input = String::new();
        if io::stdin().read_line(&mut input).is_err() {
            println!("Failed to read input.");
            continue;
        }

        let input = input.trim();
        if input.is_empty() {
            continue;
        }

        shell.history.push(input.to_string());

        if shell.handle_builtin(input) {
            continue;
        }

        if let Err(e) = shell.run_command(input) {
            eprintln!("Error: {}", e);
        }

        shell.clean_up_finished_jobs();
    }
}
