#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

// struct design for the background processes from tutorial 2 slides
typedef struct bg_process* bgRef;
typedef struct bg_process {
	pid_t pid;
	char* command;
	bgRef next;
} bg_process;

// prototype definitions
char* getUser();
char* getHost();
char* getPath();
char* getPrompt(char* user, char* host, char* path);
void process(int argc, char* args[]);
char* changeDir(int argc, char* args[]);
bgRef makeNode(pid_t pid, char* command);
bgRef insertProcess(bgRef head, pid_t pid, char* command);
bgRef backgroundProcess(int argc, char* args[], bgRef head, char* command);
char* getPathAccess(char* name);
void bglist(bgRef head);
bgRef checkBackground(bgRef head);
void cHandler(int signal);


/**
 * brief: The main function and entry point of the program.
 *
 * return: int 0: No errors
 */
int main() {

	// set up the signal handler to capture ctrl + c
	// https://coursys.sfu.ca/2017fa-cmpt-300-d1/pages/GuideToSignals/view
	struct sigaction action;
	action.sa_handler = cHandler;
	sigemptyset(&action.sa_mask);
	action.sa_flags = SA_RESTART;

	// install the action for sigint and check for failure
	if (sigaction(SIGINT, &action, NULL) == -1) {
		perror("Install failed for sigint action");
	} // if
	
	// define information for the prompt command
	char* username = getUser();
	char* hostname = getHost();
	char* path = getPath();
	char* prompt = getPrompt(username, hostname, path);
	
	// define termination and linked list head
	int bailout = 0;
	bgRef head = NULL;

	// start loop to continuously ask the prompt until shell terminates
	while (!bailout) {

		// read and store the command from user
		char* reply = readline(prompt);

		// check if reply is valid or user requests exit
		// https://coddy.tech/learn/c/logic_and_flow/the_null_terminator
		if (reply == NULL || reply[0] == '\0' || !strcmp(reply, "exit")) {
			
			bailout = 1;

		} else {

			// define variables
			int i = 0;
			char* args[200];
			int bg = 0;
			char* command = strdup(reply);

			// check if it is a bg or bglist command
			if (strlen(reply) > 2 && reply[0] == 'b' && reply[1] == 'g' 
				&& reply[2] == ' ') {
				
				// remove "bg" from the command
				// https://reactgo.com/c-remove-first-character/
				memmove(reply, reply + 3, strlen(reply));

				bg = 1;

			} else if (strlen(reply) > 5 && reply[0] == 'b' && reply[1] == 'g' 
				&& reply[2] == 'l' && reply[3] == 'i' 
				&& reply[4] == 's' && reply[5] == 't') {
				
				bg = 2;

			} // else if

			// start the string token to separate the words/arguements
			char* token = strtok(reply, " ");

			// load the words/arguements
			while (token != NULL) {
				args[i] = token;
				token = strtok(NULL, " ");
				i++;
			} // while

			// to include null terminator
			args[i] = NULL;

			// check the background processes to see if any terminated
			head = checkBackground(head);

			// check which command is being executed (bg, bglist, cd, or other)
			if (bg == 1) {
				
				// check if the command duplicated properly
				if (command == NULL) {
					perror("strdup for command failed");
				} else {
					
					// remove the first word from the command so only arguments remain
					// https://reactgo.com/c-remove-first-character/
					// https://www.geeksforgeeks.org/c/strchr-in-c/
					char* firstWord = strchr(command, ' ');
					if (firstWord == NULL) {
						free(command);
						command = " ";
					} else {
						memmove(command, firstWord, strlen(firstWord) + 1);
					} // else

				} // else

				// add the background process for execution
				head = backgroundProcess(i, args, head, command);
				free(command);

			} else if (bg == 2) {

				// list the background processes
				bglist(head);

			} else if (strcmp(args[0], "cd") == 0) {

				// change the directory and get the new path
				free(path);
				path = changeDir(i, args);

			} else {

				// run a regular process
				process(i, args);

			} // else
		} // else

		// get the new prompt with updated path and free variables bound to change
		free(reply);
		free(prompt);
		prompt = getPrompt(username, hostname, path);

	} // while
	
	// free allocated variables
	free(path);
	free(hostname);
	free(prompt);
	free(head);

	return 0;
} // main


/**
 * brief: handler function for if the user presses ctrl + c
 *
 * param: signal interrupt
 *
 * return: void
 */
void cHandler(int signal) {

	// new line in the console
	if(write(STDOUT_FILENO, "\n", sizeof("\n")) == -1) {
		perror("Error writing in handler.");
	} // if

	// fix the readline so prompt comes up again
	rl_on_new_line();
	rl_redisplay();

} // cHandler


/**
 * brief: creates a node of a background process to be 
 * added into a linked list
 *
 * param: pid - the node's id for that process
 * param: command - the command entered by the user
 *
 * return: bgRed - returns the reference to the node
 */
bgRef makeNode(pid_t pid, char* command) {
	
	// allocate and fill node
	bgRef node = (bgRef)malloc(sizeof(bg_process));
	node->next = NULL;
	node->pid = pid;
	node->command = command;
	
	return node;
} // makeNode


/**
 * brief: inserts the node in the linked list of background processes
 *
 * param: head - the head of the linked list
 * param: pid - id for that process
 * param: command - the command entered by the user
 *
 * return: bgRed - returns the reference to the head
 */
bgRef insertProcess(bgRef head, pid_t pid, char* command) {
	
	// insert process node in the list
	bgRef node = makeNode(pid, command);
	node->next = head;
	head = node;

	return head;
} // insertProcess


/**
 * brief: checks if any of the background processes have terminated
 *
 * param: head - the head of the linked list
 *
 * return: bgRed - returns the reference to the head
 */
bgRef checkBackground(bgRef head) {

	// define variables
	bgRef curr = head;
	bgRef prev = NULL;
	int status;
	int pid = 0;

	// iterrate through the linked list and check for ended processes
	while (curr != NULL) {
		pid = curr->pid;

		// check if process with that pid has terminated
		// https://linux.die.net/man/2/waitpid
		if (waitpid(pid, &status, WNOHANG) != pid) {
			printf("%d:  %s has terminated\n", curr->pid, curr->command);
			
			// remove the background process node from linked list
			// https://www.geeksforgeeks.org/c/c-program-for-deleting-a-node-in-a-linked-list/
			if (prev == NULL) {
				head = curr->next;
				free(curr->command);
				free(curr);
				curr = head;
			} else {
				prev->next = curr->next;
				free(curr->command);
				free(curr);
				curr = prev->next;
			} // else

		} else {
			
			// process hasn't terminate so continue
			prev = curr;
			curr = curr->next;

		} // else
			
	} // while
	
	return head;
} // checkBackground


/**
 * brief: lists out the current background processes 
 *
 * param: head - the head of the linked list
 *
 * return: void
 */
void bglist(bgRef head) {
	
	// variable definitions
	bgRef curr = head;
	int count = 0;
	
	// iterrate through bg linked list and print
	while (curr != NULL) {
		printf("%d:  %s\n", curr->pid, curr->command);
		curr = curr->next;
		count++;
	} // while

	printf("Total Background Jobs:  %d\n", count);
} // bglist


/**
 * brief: creates and executes a background process
 *
 * param: argc - number of arguments in the user command including the command
 * param: args - arguments for the command entered by the user
 * param: head - the head of the linked list
 * param: command - the user input with the command replaced by the path
 *
 * return: bgRed - reference to the head
 */
bgRef backgroundProcess(int argc, char* args[], bgRef head, char* command) {
	
	// variable definition
	pid_t pid;

	// fork and execute the process in the background
	// process creation based on tutorial 2 slides
	if ((pid = fork()) == 0) {
		
		// output the bg process execution to a null file
		// https://medium.com/@ssreehari/building-a-bash-like-application-e17122609be4
		int nullFile = open("/dev/null", O_WRONLY);
		if (nullFile < 0) {
			perror("error with opening null file");
			exit(1);
		} // if

		dup2(nullFile, 1);
		dup2(nullFile, 2);
		close(nullFile);

		// execute the process
		execvp(args[0], args);
		perror("execvp failed");
		exit(1);

	} else if (pid < 0) {
		perror("Error in the fork for background");
	} else {

		// get the path for the command and its arguments in one string
		char* path = getPathAccess(args[0]);
		int buffer = snprintf(NULL, 0, "%s %s ", path, command) + 1;
		char* fullCommand = (char*)malloc(buffer*sizeof(char));
		
		if (snprintf(fullCommand, buffer, "%s %s ", path, command) == -1) {
			perror("making fullCommand with path failed");
		} // if

		// insert the background process
		head = insertProcess(head, pid, fullCommand);
		free(path);

	} // else

	return head;
} // backgroundProcess


/**
 * brief: gets the path of the command using which
 *
 * param: name - the user command to find the path for
 *
 * return: char* - string of the command path
 */
char* getPathAccess(char* name) {
	
	// create a string with "which name" for finding path
	int buffer = snprintf(NULL, 0, "which %s", name) + 1;
	char* fileName = (char*)malloc(buffer*sizeof(char));

	if (snprintf(fileName, buffer, "which %s", name) == -1) {
		perror("Error with getting which for fileName");
	} // if

	// get the path through piping with which
	// logic for getting the path from tutorial 3 slides
	FILE* fp = popen(fileName, "r");

	if (fp == NULL) {
		perror("Error with popen");
		return NULL;
	} // if

	// https://opensource.com/article/22/5/safely-read-user-input-getline
	char* path = NULL;
	size_t pbuffer = 0;
	int num = getline(&path, &pbuffer, fp);

	// check that getting the path worked
	if (num == -1) {
		perror("error with path");
		free(path);
		return NULL;
	} // if

	// check that the file closed well
	if (pclose(fp) != 0) {
		perror("Error closing file");
		free(path);
		return NULL;
	} // if

	// strip the trailing '\n'
	// https://www.geeksforgeeks.org/dsa/removing-trailing-newline-character-from-fgets-input/
	// https://www.geeksforgeeks.org/c/strchr-in-c/
	char* trailing = strchr(path, '\n');
    
	if (trailing != NULL) {
		*trailing = '\0';
    } // if

	return path;
} //getPathAccess

/**
 * brief: changes the current working directory 
 *
 * param: argc - number of arguments in the user command including the command
 * param: args - arguments for the command entered by the user
 *
 * return: char* - string of the new path after changing directory
 */
char* changeDir(int argc, char* args[]) {

	// https://www.geeksforgeeks.org/linux-unix/chdir-in-c-language-with-examples/ - for chdir
	// checks if tidle is used
	if (argc == 1 || strcmp(args[1], "~") == 0) {
		
		// get the path for the user's home and cd
		char* home = getenv("HOME");

		if (home == NULL) {
			perror("Failed to get home directory");
		} else {
			chdir(home);
		} // else

	} else if (args[1][0] == '~') {
		
		// get the user's home directory
		char* home = getenv("HOME");

		if (home == NULL) {
			perror("Failed to get home directory");
		} else {
			// add the home directory to the path in replace of the tidle and cd
			// https://reactgo.com/c-remove-first-character/ for removing the first word
			memmove(args[1], args[1] + 1, strlen(args[1]));
			strcat(home, args[1]);
			chdir(home);
		} // else

	} else {
		
		// change the directory
		chdir(args[1]);
	} // else

	// get the new path and return it
	return getPath();

}  // changeDir


/**
 * brief: executes the process
 *
 * param: argc - number of arguments in the user command including the command
 * param: args - arguments for the command entered by the user
 *
 * return: void
 */
void process(int argc, char* args[]) {

	// variable definitions
	int pid;
	int status;

	// check to see if the pid is in the child or failed
	// logic based on the tutorial 2 slides
	if ((pid = fork()) == 0) {
		
		// execute the command
		execvp(args[0], args);
		perror("execvp failed");
		exit(1);

	} else if (pid < 0) {
	
		perror("Error in the fork");
	
	} // else if

	// wait for the child to finish
	while (wait(&status) > 0) {
		;
	} // while

} // process

/**
 * brief: gets the name of the user
 *
 * return: char* - string of the username
 */
char* getUser() {

	// variable definitions
	char* user = (char*)malloc(30*sizeof(char));
	user = getlogin();

	if (user == NULL) {
		perror("Username failed");
	} // if

	return user;
} // getUser


/**
 * brief: gets the name of the host
 *
 * return: char* - string of the hostname
 */
char* getHost() {

	// variable definitions
	int buffer = 20;
	char* host = (char*)malloc(buffer*sizeof(char));

	if (gethostname(host, buffer) == -1) {
		perror("Hostname failed");
	} // if

	return host;
} // getHost


/**
 * brief: gets the path of the current working directory
 *
 * return: char* - string of the path
 */
char* getPath() {

	char* path = getcwd(NULL, 0);

	if (path == NULL) {
		perror("Path failed");
	} // if
	
	return path;
} // getPath


/**
 * brief: puts together the name, host, and user strings to get the full user prompt
 *
 * param: user - string of the username
 * param: host - string of the hostname
 * param: path - string of the current directory path
 *
 * return: char* - string containing the user prompt
 */
char* getPrompt(char* user, char* host, char* path) {

	// variable definitions
	int buffer = snprintf(NULL, 0, "%s@%s: %s > ", user, host, path) + 1;
	char* prompt = (char*)malloc(buffer*sizeof(char));

	if (snprintf(prompt, buffer, "%s@%s: %s > ", user, host, path) == -1){
		perror("Path failed");
	} // if
	
	return prompt;
} // getPrompt
