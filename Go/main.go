package main

import (
	"bufio"
	"fmt"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"syscall"
)

var history []string
var aliases = make(map[string]string)
var jobs []Job

type Job struct {
	Pid     int
	Command string
	Status  string // "Running", "Stopped", or "Done"
}

func main() {
	// Create a scanner to read input from the user
	scanner := bufio.NewScanner(os.Stdin)
	fmt.Println("Welcome to the custom shell. Type 'exit' to quit.")

	// Start an infinite loop to take user input
	for {
		// Display a custom prompt
		fmt.Print("custom-shell>>> ")

		// Read the user input
		scanner.Scan()
		input := scanner.Text()

		// If the user enters 'exit', break out of the loop
		if input == "exit" {
			fmt.Println("Exiting shell.")
			break
		}

		// Add the command to the history
		history = append(history, input)

		// Process the input: check for piping, redirection, or built-in commands
		if strings.HasPrefix(input, "alias") {
			processAlias(input)
		} else if strings.HasPrefix(input, "cd") {
			processCd(input)
		} else if strings.HasPrefix(input, "pwd") {
			processPwd()
		} else if strings.HasPrefix(input, "history") {
			processHistory()
		} else if strings.HasPrefix(input, "jobs") {
			processJobs()
		} else if strings.HasPrefix(input, "fg") {
			processFg(input)
		} else if strings.HasPrefix(input, "bg") {
			processBg(input)
		} else {
			// Replace any aliases with the corresponding command
			input = processAliases(input)

			// Check for background process request
			background := strings.HasSuffix(input, "&")
			if background {
				input = strings.TrimSpace(input[:len(input)-1]) // Remove '&'
			}

			// Process piping and redirection
			processRedirectionAndPipes(input, background)
		}
	}
}

// processCommand executes the command
func processCommand(input string, background bool) {
	// Split the input into the command and its arguments
	args := strings.Fields(input)
	cmdName := args[0]
	cmdArgs := args[1:]

	// Create the command to run
	cmd := exec.Command(cmdName, cmdArgs...)

	// If running in background, do not wait for the command to complete
	if background {
		cmd.SysProcAttr = &syscall.SysProcAttr{Setpgid: true}
	} else {
		cmd.SysProcAttr = &syscall.SysProcAttr{Setpgid: false}
	}

	// Run the command
	cmdOutput, err := cmd.CombinedOutput()
	if err != nil {
		fmt.Println("Error executing command:", err)
	} else {
		// Print the command's output
		fmt.Printf("%s\n", cmdOutput)
	}

	// If it's a background process, record it
	if background {
		jobs = append(jobs, Job{
			Pid:     cmd.Process.Pid,
			Command: input,
			Status:  "Running",
		})
	}
}

// processAlias handles the creation of command aliases
func processAlias(input string) {
	parts := strings.Fields(input)
	if len(parts) < 3 {
		fmt.Println("Usage: alias <name> <command>")
		return
	}

	// Define the alias
	aliasName := parts[1]
	aliasCommand := strings.Join(parts[2:], " ")

	// Store the alias
	aliases[aliasName] = aliasCommand
	fmt.Printf("Alias '%s' created for command: %s\n", aliasName, aliasCommand)
}

// processAliases expands any aliases present in the input
func processAliases(input string) string {
	// Check if the input starts with an alias
	words := strings.Fields(input)
	if len(words) > 0 {
		// Check if it's an alias
		if aliasCommand, found := aliases[words[0]]; found {
			// Replace the alias with its actual command
			input = strings.Replace(input, words[0], aliasCommand, 1)
		}
	}
	return input
}

// processCd changes the directory
func processCd(input string) {
	// Strip the 'cd ' part of the input
	args := strings.Fields(input)
	if len(args) < 2 {
		fmt.Println("cd: missing argument")
		return
	}

	// Change the directory
	err := os.Chdir(args[1])
	if err != nil {
		fmt.Println("cd error:", err)
	} else {
		fmt.Printf("Changed directory to %s\n", args[1])
	}
}

// processPwd prints the current working directory
func processPwd() {
	dir, err := os.Getwd()
	if err != nil {
		fmt.Println("pwd error:", err)
	} else {
		fmt.Println("Current directory:", dir)
	}
}

// processHistory prints the command history
func processHistory() {
	if len(history) == 0 {
		fmt.Println("No command history available.")
		return
	}
	for i, cmd := range history {
		fmt.Printf("%d: %s\n", i+1, cmd)
	}
}

// processJobs displays the list of jobs running in the background
func processJobs() {
	if len(jobs) == 0 {
		fmt.Println("No background jobs.")
		return
	}
	for i, job := range jobs {
		fmt.Printf("[%d] %d %s %s\n", i+1, job.Pid, job.Status, job.Command)
	}
}

// processFg brings a job to the foreground
func processFg(input string) {
	parts := strings.Fields(input)
	if len(parts) < 2 {
		fmt.Println("Usage: fg <job_id>")
		return
	}

	jobID := parts[1]
	jobIndex, err := strconv.Atoi(jobID)
	if err != nil || jobIndex <= 0 || jobIndex > len(jobs) {
		fmt.Println("Invalid job ID.")
		return
	}

	job := &jobs[jobIndex-1]
	job.Status = "Running"

	// Bring the job to the foreground by using a Wait for the command to finish
	process, err := os.FindProcess(job.Pid)
	if err != nil {
		fmt.Println("Error finding process:", err)
		return
	}

	// Send SIGCONT to the process if it's stopped, and wait for it to complete
	err = process.Signal(syscall.SIGCONT)
	if err != nil {
		fmt.Println("Error sending signal to process:", err)
	}
	process.Wait()
	job.Status = "Done"
}

// processBg sends a job to the background
func processBg(input string) {
	parts := strings.Fields(input)
	if len(parts) < 2 {
		fmt.Println("Usage: bg <job_id>")
		return
	}

	jobID := parts[1]
	jobIndex, err := strconv.Atoi(jobID)
	if err != nil || jobIndex <= 0 || jobIndex > len(jobs) {
		fmt.Println("Invalid job ID.")
		return
	}

	job := &jobs[jobIndex-1]
	job.Status = "Running"

	// Send SIGCONT to the process to resume it
	process, err := os.FindProcess(job.Pid)
	if err != nil {
		fmt.Println("Error finding process:", err)
		return
	}

	err = process.Signal(syscall.SIGCONT)
	if err != nil {
		fmt.Println("Error sending signal to process:", err)
	}
}

// processRedirectionAndPipes handles redirection (>, >>, <) and pipes (|)
func processRedirectionAndPipes(input string, background bool) {
	// Handle piping first
	if strings.Contains(input, "|") {
		parts := strings.Split(input, "|")
		if len(parts) > 1 {
			// Process each part of the pipe separately
			processPipes(parts, background)
			return
		}
	}

	// Handle redirection
	if strings.Contains(input, ">") || strings.Contains(input, ">>") || strings.Contains(input, "<") {
		processRedirection(input, background)
		return
	}

	// Regular command without pipes or redirection
	processCommand(input, background)
}

// processPipes processes piped commands (e.g., ls | grep txt)
func processPipes(parts []string, background bool) {
	// Create pipes between commands
	var prevCmd *exec.Cmd
	for i, part := range parts {
		part = strings.TrimSpace(part)
		args := strings.Fields(part)
		cmd := exec.Command(args[0], args[1:]...)

		// Set up piping between commands
		if i > 0 {
			pipeReader, pipeWriter := prevCmd.StdoutPipe()
			cmd.Stdin = pipeReader
			cmd.Stdout = pipeWriter
		}

		// If it's the last command in the pipe chain, output to the terminal
		if i == len(parts)-1 {
			cmd.Stdout = os.Stdout
		}

		// Execute the command
		if err := cmd.Start(); err != nil {
			fmt.Println("Error starting pipe:", err)
			return
		}

		// Set the previous command to be the current one for piping
		prevCmd = cmd
	}

	// Wait for the last command to finish
	if prevCmd != nil {
		if err := prevCmd.Wait(); err != nil {
			fmt.Println("Error executing piped command:", err)
		}
	}
}

// processRedirection handles output redirection (>) and input redirection (<)
func processRedirection(input string, background bool) {
	// Split the input based on redirection operators
	var cmdInput string
	var redirectionOutput string
	var appendMode bool
	if strings.Contains(input, ">") {
		// Output redirection: cmd > file
		parts := strings.Split(input, ">")
		cmdInput = strings.TrimSpace(parts[0])
		redirectionOutput = strings.TrimSpace(parts[1])
	} else if strings.Contains(input, ">>") {
		// Append output redirection: cmd >> file
		parts := strings.Split(input, ">>")
		cmdInput = strings.TrimSpace(parts[0])
		redirectionOutput = strings.TrimSpace(parts[1])
		appendMode = true
	} else if strings.Contains(input, "<") {
		// Input redirection: cmd < file
		parts := strings.Split(input, "<")
		cmdInput = strings.TrimSpace(parts[0])
		redirectionOutput = strings.TrimSpace(parts[1])
	}

	// Execute the command
	cmd := exec.Command(cmdInput)
	if appendMode {
		file, err := os.OpenFile(redirectionOutput, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
		if err != nil {
			fmt.Println("Error opening file for append:", err)
			return
		}
		cmd.Stdout = file
	} else {
		file, err := os.Create(redirectionOutput)
		if err != nil {
			fmt.Println("Error creating file:", err)
			return
		}
		cmd.Stdout = file
	}

	// Run the command
	err := cmd.Run()
	if err != nil {
		fmt.Println("Error executing command with redirection:", err)
	}
}
