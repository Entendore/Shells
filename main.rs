use std::env;
use std::io::{self, Write};
use std::process::{exit, Command};

fn main() {
    loop {
        // Print the prompt
        print!("rust-shell> ");
        io::stdout().flush().unwrap(); // Make sure the prompt is displayed immediately

        // Read user input
        let mut input = String::new();
        io::stdin().read_line(&mut input).unwrap();

        // Trim the newline character
        let input = input.trim();

        // Exit the shell if the user types "exit"
        if input == "exit" {
            println!("Exiting shell...");
            exit(0);
        }

        // Handle `cd` command
        if input.starts_with("cd ") {
            let path = input.split_at(3).1.trim(); // Extract path from "cd <path>"
            if let Err(e) = change_directory(path) {
                eprintln!("{}", e);
            }
            continue;
        }

        // Split input into a command and its arguments
        let args: Vec<&str> = input.split_whitespace().collect();

        if args.is_empty() {
            continue;
        }

        // Run the command
        let output = Command::new(args[0]).args(&args[1..]).output();

        match output {
            Ok(output) => {
                if !output.stdout.is_empty() {
                    print!("{}", String::from_utf8_lossy(&output.stdout));
                }

                if !output.stderr.is_empty() {
                    eprint!("{}", String::from_utf8_lossy(&output.stderr));
                }

                if !output.status.success() {
                    eprintln!("Command failed with status: {}", output.status);
                }
            }
            Err(e) => eprintln!("Failed to execute command: {}", e),
        }
    }
}

// Function to handle changing directories
fn change_directory(path: &str) -> Result<(), String> {
    if path.is_empty() {
        // If no path is given, change to the home directory
        if let Some(home_dir) = env::var("HOME").ok() {
            return env::set_current_dir(home_dir)
                .map_err(|e| format!("Failed to change directory: {}", e));
        } else {
            return Err("Home directory not found".to_string());
        }
    }

    match env::set_current_dir(path) {
        Ok(_) => {
            // Print the new directory
            if let Ok(new_dir) = env::current_dir() {
                println!("Changed directory to: {}", new_dir.display());
            }
            Ok(())
        }
        Err(e) => Err(format!("Failed to change directory: {}", e)),
    }
}
