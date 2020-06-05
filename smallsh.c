/****************************************************************************************
* Name: Charles Kusk
* Date: May 21, 2020
* Description: Shell written in C
****************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <fcntl.h>
#include <signal.h>

#define CMD_LINE_LENGTH 2048
#define MAX_ARGS	512
#define JUNK_VAL	-5

void catchSIGINT(int);
void catchSIGTSTP(int);
void commandPrompt();
void createSignalHandlers();
void expand$$(char*);
int parseInput(char*, char**);
void ioRedirection(char**, int*);
void trimArray(char**, int*, int);
void resetArgArray(char**, int*);
void printArgs(char**, int*);
void Exit();
void cd(char*);
void status(int);

// Foreground only mode.  Needs to be global so signal handers can access
bool fgom = false;	

// Made signal structs global to be accessec anywhere
struct sigaction SIGINT_action = {0};
struct sigaction SIGTSTP_action = {0};

int main()
{
	commandPrompt();
	return 0;
}

/****************************************************************************************
*: Description: Signal handler for CTRL+Z
****************************************************************************************/
void catchSIGTSTP(int signo)
{
	// Switch mode and display message to user
	if(!fgom)
	{
		fgom = true;
		char* message = "\nEntering foreground-only mode (& is now ignored)\n";
		write(STDOUT_FILENO, message, 50);
		fflush(stdout);
	}
	else if(fgom)
	{
		fgom = false;
		char* message = "\nExiting foreground-only mode\n";
		write(STDOUT_FILENO, message, 30);
		fflush(stdout);
	}
}

/****************************************************************************************
*: Description: Shell written in C
****************************************************************************************/
void commandPrompt()
{
	createSignalHandlers();

	char userInput[CMD_LINE_LENGTH];	
	char *argArray[MAX_ARGS];
	int numArgs = 0;

	// Will be used to fork() program
	pid_t spawnPid = JUNK_VAL;

	// Not sure if needed, used in lecture fo waitpid function
	int childExitStatus = JUNK_VAL;

	// Infinite loop, always wanto to prompt user
	while(1)
	{
		// Resets char array every time through loop
		memset(userInput, '\0', CMD_LINE_LENGTH);	

		// Resets array
		resetArgArray(argArray, &numArgs);	

		// Terminal prompt is colon, flush every time
		printf(": ");
		fflush(stdout);

		// Gets input from user at command line
		if(fgets(userInput, CMD_LINE_LENGTH, stdin) == NULL)
		{
			continue;
		}

		// Reprompt user if input is null, blank, or a comment
		if(userInput == NULL || userInput[0] == '\n' || userInput[0] == '#')
		{
			continue;
		}
	
		// Searches input for $$ and replaces with processID before parsing
		expand$$(userInput);

		// Split string into function and arguments(returns number of arguments)
		numArgs = parseInput(userInput, argArray);

		// Boolean to track if process is to be in background
		bool backgroundProcess = false;
	
		// If last argument is '&' we run process in background
		if(strcmp(argArray[numArgs-1], "&") == 0)
		{
			backgroundProcess = true;
		}
	
		// Current working directory, will remain NULL if no 2nd argument passed in
		char *cwd = NULL;

		// Three built in commands
		if(strcmp(argArray[0], "exit") == 0)
			Exit();
		else if(strcmp(argArray[0], "cd") == 0)
		{	
			// Changes cwd to passed in directory instead of NULL
			if(numArgs > 1)
				cwd = argArray[1];		
			cd(cwd);
		}

		else if(strcmp(argArray[0], "status") == 0)
			status(childExitStatus);
		// Search root for commands if not a built in one
		else
		{
			// If fg mode and a & appears
			if(fgom && backgroundProcess)
			{
				// Don't enter next if to become background process
				backgroundProcess = false;
				// Remove & before sending to exec
				trimArray(argArray, &numArgs, numArgs-1);
			}
		
			// spawnPid is the PID of the child process
			spawnPid = fork();
			switch(spawnPid)
			{
				// Error Case
				case -1:
				{
					perror("Fork() did not work!\n");
					exit(1);
					break;
				}
				// child process
				case 0:
				{
					// Any redirection must occur before execvp function 
					ioRedirection(argArray, &numArgs);
			
					// If background process, redirect stdin
					if(backgroundProcess)
					{
						int input, result;
						input = open("/dev/null", O_RDONLY);
						if(input == -1)
						{
							perror("open()");
							exit(1);
						}
						// Sendds stdout from process to null
						result = dup2(input, 0);
						if(result == -1)
						{
							perror("dup2()");
							exit(1);
						}	
						// Remove & before sending to exec
						trimArray(argArray, &numArgs, numArgs-1);
					}
					// If not background process, be killable by ctrlc
					else
					{
						// Want child process to be killed by ctrlc	
						SIGINT_action.sa_handler = SIG_DFL;
						sigaction(SIGINT, &SIGINT_action, NULL);
					}

					// exec will remove any signal handlers
					// only use SIG_DFL or SIG_IGN	
					// Searched PATH for argArray[0] and runs on argArray
					execvp(*argArray, argArray);
					
					// Program only reaches these lines if there is an error
					perror(argArray[0]);

					exit(1);
					break;
				}
				// Parent process
				default:
				{
					// Display pid of background process
					if(backgroundProcess)
					{
						printf("background pid is %i\n", spawnPid);
						fflush(stdout);
						break;
					}
					// Waits for spawnPid process to end, exit coes goes to childExitStatus
					spawnPid = waitpid(spawnPid, &childExitStatus, 0);					
		
					// Tell user if process killed by signal	
					if(WIFSIGNALED(childExitStatus))
					{
						printf("terminated by signal %i\n", WTERMSIG(childExitStatus));
						fflush(stdout);
					}					
					break;
				}
			}
		}
		// At end of every while loop check for any completed processes
		pid_t spawn2 = waitpid(-1, &childExitStatus, WNOHANG);
		if(spawn2 > 0)
		{
			printf("background pid %i is done: ", spawn2);
			fflush(stdout);
			status(childExitStatus);
		}
	}
}

/****************************************************************************************
* Description: Set up sig handler structs
*****************************************************************************************/
void createSignalHandlers()
{
	// Set up signal handlers, taken from lecture 3.3
	// Want to ignore by default then turn on for children
	SIGINT_action.sa_handler = SIG_IGN;
	sigfillset(&SIGINT_action.sa_mask);
	SIGINT_action.sa_flags = 0;
	sigaction(SIGINT, &SIGINT_action, NULL);

	SIGTSTP_action.sa_handler = catchSIGTSTP;
	sigfillset(&SIGTSTP_action.sa_mask);
	SIGTSTP_action.sa_flags = 0;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);
}


/****************************************************************************************
* Description: Search input for "$$" and replace with process ID 
*****************************************************************************************/
void expand$$(char *userInput)
{
	// Should have address starting at first character of input
	int index = 0;
	int index2 = strlen(userInput);
	// Accounts for newline and zero indexing
	index2--;
	index2--;
	int pidLength = 0;
	pid_t pid = getpid();
	char cpid[6];

	// pidLength will be number of digits in pid 
	int z = pid; 
	while(z != 0)
	{
		z = z / 10; 
		pidLength++;
	}

	// Converts integer to string
	sprintf(cpid, "%i", pid);	

	// Loop through every character of the array
	while(userInput[index] != '\n')
	{
		// If two consecutive '$' replace with process ID
		if(userInput[index] == '$' && userInput[index+1] == '$')
		{
			// If not enough room in buffer (2048 chars)
			if(index + (pidLength - 2) > CMD_LINE_LENGTH)
			{
				perror("Not enough room in buffer to add process ID\n");
			}
			// If enough room proceed
			else
			{
				// Move backwards through array until reaching second $
				while(index2 != index + 1)
				{
					// Starting at null terminator, move all chars 4 spots back
					// -2 because the original $$ will not be included
					userInput[index2 + pidLength - 2] = userInput[index2];		
					index2--;
				}
				// Replace both '$' and 2 chars after with PID
				for(int x = 0; x < pidLength; x++)
					userInput[index + x] = cpid[x];
			}
		}
		index++;
	}
}

/****************************************************************************************
* Description: Takes input given by user, parses it into the command and fills the 
*              argArray with appropriate arguments
*****************************************************************************************/
int parseInput(char *userInput, char **argArray)
{
	// If we have zero, loop restarts anyways
	int numArgs = 1;

	argArray[0] = strtok(userInput, " \n");

	while(argArray[numArgs] = strtok(NULL, " \n"))
	{
		numArgs++;
	}

	// Function returns number of arguments
	return numArgs;
}


/****************************************************************************************
* Description: Checks for '>' and '<' and redirects i/o as needed 
*****************************************************************************************/
void ioRedirection(char **argArray, int *numArgs)
{
	for(int i = 0; i < *numArgs; i++)
	{
		// Output redirection
		if(strcmp(argArray[i], ">") == 0)
		{
			// Output will be directed to filename following >
			int targetFD = open(argArray[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
			// If file could not be opened
			if(targetFD == -1)
			{
				char errorMessage[100];
				sprintf(errorMessage, "cannot open %s for output", argArray[i+1]);
				perror(errorMessage);
				exit(1);
			}
			// Redirect output from stdout to file
			int result = dup2(targetFD, 1);
			// If file pointer could not be redirected
			if(result == -1)
			{
				perror("target dup2");
				exit(2);
			}
		
			// Removes '>' and file from array	
			trimArray(argArray, numArgs, i);
			trimArray(argArray, numArgs, i);
	
			// Need to move loop counter back one
			i -= 1;
		}
		// Input redirection
		else if(strcmp(argArray[i], "<") == 0)
		{
			// Output will be directed to filename following >
			int sourceFD = open(argArray[i+1], O_RDONLY);
			// If file could not be opened
			if(sourceFD == -1)
			{
				char errorMessage[100];
				sprintf(errorMessage, "cannot open %s for input", argArray[i+1]);
				perror(errorMessage);
				exit(1);
			}
			// Redirect output from stdout to file
			int result = dup2(sourceFD, 0);
			// If file pointer could not be redirected
			if(result == -1)
			{
				perror("source dup2");
				exit(2);
			}
		
			// Removes '<' and file from array	
			trimArray(argArray, numArgs, i);
			trimArray(argArray, numArgs, i);
	
			// Need to move loop counter back one
			i -= 1;
		}
	}
}

/****************************************************************************************
* Description: Deletes element at specified index then shifts other elements down one 
****************************************************************************************/
void trimArray(char **argArray, int *numArgs, int index)
{
	for(int j = index; j < *numArgs - 1; j++)
	{
		// Set every pointer to next pointer in array
		argArray[j] = argArray[j+1];
	}
	// One less argument with > removed
	*numArgs = *numArgs - 1;
	// Set former last element to be NULL
	argArray[*numArgs] = NULL;
}

/****************************************************************************************
* Description: Resets argArray and frees memory 
****************************************************************************************/
void resetArgArray(char **argArray, int *numArgs)
{
	// numArgs is one more than array size
	for(int i = 0; i < *numArgs; i++)
	{
		argArray[i] = NULL;
	}

	// Sets numArgs back to zero
	*numArgs = 0;	
}

/****************************************************************************************
* Description: Displays all strings in the array, for troubleshooting 
****************************************************************************************/
void printArgs(char **argArray, int *numArgs)
{
	for(int i = 0; i < *numArgs; i++)
	{
		printf("Arg #%i: %s\n", i, argArray[i]);	
		fflush(stdout);
	}
}

/****************************************************************************************
* Description: Exits shell, must kill any process that shell started before terminating 
****************************************************************************************/
void Exit()
{
	exit(0);
}


/****************************************************************************************
* Description: Changes working directory of shell.  If no arguments changes directory to 
*              HOME. Command can also take one argument, the path of teh directory to 
*              change to.  Supports absolute and relative paths.
****************************************************************************************/
void cd(char *cwd)
{
	if(cwd == NULL)
	{
		// If no arguments
		chdir(getenv("HOME"));
	}
	else
	{
		if(chdir(cwd) == -1)
		{
			perror("Could not change directory!\n");
		}
	}
}

/****************************************************************************************
* Description: Prints out the exit statur or terminating signal of last foreground 
*              process ran by shell.  If ran before any foreground command return exit
*              status 0.
****************************************************************************************/
void status(int childExitMethod)
{
	// PRINT EXIT SIGNAL OF LAST FOREGROUND PROCESS
	if(WIFEXITED(childExitMethod))
	{
		printf("exit value %i\n", WEXITSTATUS(childExitMethod));
		fflush(stdout);
	}
	else if(WIFSIGNALED(childExitMethod))
	{
		printf("terminated by signal %i\n", WTERMSIG(childExitMethod));
		//printf("terminated by signal %i\n", childExitMethod);
		fflush(stdout);
	}
	else
	{
		printf("exit status unknown\n");
		fflush(stdout);
	}
}
