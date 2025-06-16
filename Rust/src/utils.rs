use std::fs::{File, OpenOptions};
use std::process::Child;

#[derive(Debug)]
pub struct Job {
    pub id: usize,
    pub command: String,
    pub process: Child,
}

// Parses and extracts IO redirection if present
pub fn redirect_stdio(
    tokens: &[String],
) -> Result<(&str, Vec<String>, Option<File>, Option<File>, bool), Box<dyn std::error::Error>> {
    let mut args = Vec::new();
    let mut stdin_file = None;
    let mut stdout_file = None;
    let mut append = false;

    let mut i = 1;
    while i < tokens.len() {
        match tokens[i].as_str() {
            "<" => {
                if i + 1 < tokens.len() {
                    stdin_file = Some(File::open(&tokens[i + 1])?);
                    i += 1;
                }
            }
            ">" => {
                if i + 1 < tokens.len() {
                    stdout_file = Some(
                        OpenOptions::new()
                            .create(true)
                            .write(true)
                            .truncate(true)
                            .open(&tokens[i + 1])?,
                    );
                    append = false;
                    i += 1;
                }
            }
            ">>" => {
                if i + 1 < tokens.len() {
                    stdout_file = Some(
                        OpenOptions::new()
                            .create(true)
                            .append(true)
                            .open(&tokens[i + 1])?,
                    );
                    append = true;
                    i += 1;
                }
            }
            _ => args.push(tokens[i].clone()),
        }
        i += 1;
    }

    Ok((&tokens[0], args, stdin_file, stdout_file, append))
}
