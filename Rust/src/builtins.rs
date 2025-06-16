use crate::shell::Shell;

pub fn handle_builtin(command: &Vec<String>, shell: &mut Shell) -> bool {
    if command.is_empty() {
        return false;
    }
    match command[0].as_str() {
        "cd" => {
            let target = command.get(1).map_or("/", String::as_str);
            shell.change_dir(target);
            true
        }
        "exit" => {
            shell.exit();
            true
        }
        "history" => {
            for (idx, cmd) in shell.history.iter().enumerate() {
                println!("{}: {}", idx + 1, cmd);
            }
            true
        }
        _ => false,
    }
}
