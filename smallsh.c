#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h> 
#include <sys/wait.h>
#include <fcntl.h>

//global variable: 1 is foreground only mode (& has no effect), 0 is normal
int fg_mode = 0;

/* struct for everything in command */
struct full_command
{
    char *command;
    char *args[512];
    char *input_file;
    char *output_file;
    int bg;
    int num_args;
};

char* getCommand()
{
    char *input = calloc(2048, sizeof(char));
    printf(": ");
    fflush(stdout);

    fgets(input, 2048, stdin);

    //drop newline character
    input[strcspn(input, "\n")] = 0;

    return input;
}

char *expand$$(char *input)
{
    //This function takes a string as a parameter. It finds any instance of $$ and replaces
    //it with the process id. 
    char *pid = calloc(10, sizeof(char));
    sprintf(pid, "%d", getpid());

    char *expanded_input = calloc(2048, sizeof(char));

    char *$$_ptr = strstr(input, "$$");

    //Find every instnce of $$ and replace with pid
    while($$_ptr != NULL)
    {
        int index_of_$$ = $$_ptr - input;

        strncat(expanded_input, input, index_of_$$);
        strcat(expanded_input, pid);
        index_of_$$ += 2;
        input = &input[index_of_$$];

        $$_ptr = strstr(input, "$$");
    }
    
    strcat(expanded_input, input);

    return expanded_input;
}

struct full_command *parseCommand(char *user_input)
{
    char* input = expand$$(user_input);
    
    struct full_command *new_command = malloc(sizeof(struct full_command));

    new_command->args[0] = NULL;
    int arg_index = 0;

    //Get command portion of full command
    new_command->command = strtok(input, " ");

    char *token = strtok(NULL, " ");
    while(token != NULL)
    {
        //if the final symbol is &, bg is set to 1 and the process runs in the background 
        //but if & is not the final word it is an argument
        if (token != NULL && strcmp(token, "&") == 0)
        {
            token = strtok(NULL, " ");
            
            if (token == NULL)
            {
                if (fg_mode != 1)
                    new_command->bg = 1;
                    break;
            }
            else
            {
                new_command->args[arg_index] = "&";
                arg_index++;
            }
            
        }

        //get input file if present
        if(strcmp(token, "<") == 0)
        {
            token = strtok(NULL, " ");
            new_command->input_file = token;
        }
        //get output file if present
        else if(strcmp(token, ">") == 0)
        {
            token = strtok(NULL, " ");
            new_command->output_file = token;
        }
        //get arguments
        else
        {
            new_command->args[arg_index] = token;
            arg_index++;
        }

        token = strtok(NULL, " ");


    }

    new_command->num_args = arg_index;
    return new_command;
}

void cd(char* dir)
{
    int chdir_status = 0;
    //change to directory provided as argument
    if(dir != NULL)
    {
        chdir_status = chdir(dir);
    }
    //no argument so change to home directory
    else
    {
        chdir(getenv("HOME"));
    }

    //invalid directory provided as argument
    if (chdir_status == -1)
    {
        printf("The directory '%s' does not exist\n", dir);
        fflush(stdout);
    }
}

void status(int childStatus)
{
    //From the Proecess API Exploration
    if(WIFEXITED(childStatus))
    {
	    printf("Exit value %d\n", WEXITSTATUS(childStatus));
        fflush(stdout);
	} 
    else
    {
		printf("Terminated by signal %d\n", WTERMSIG(childStatus));
        fflush(stdout);
	}
}

void redirect_input(char *input_file)
{
    //redirect input from stdin to input_file

    //From processes and I/O exploration

    // Open input file
    int sourceFD = open(input_file, O_RDONLY);
    if (sourceFD == -1) 
    { 
        printf("Cannot open %s for input\n", input_file); 
        fflush(stdout);
        exit(1); 
    }

    // Redirect stdin to source file
    int result = dup2(sourceFD, 0);
    if (result == -1) 
    { 
        perror("source dup2()"); 
        fflush(stdout);
        exit(2); 
    }
}

void redirect_output(char *output_file)
{
    //redirect output from stdout to output_file

    //From processes and I/O exploration

    //Open output file
    int targetFD = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (targetFD == -1) 
    {
        perror("Error");
        fflush(stdout);
        exit(1);
    }

    // Use dup2 to point FD 1, i.e., standard output to targetFD
    int result = dup2(targetFD, 1);
    if (result == -1) 
    {
        perror("Error"); 
        fflush(stdout);
        exit(1); 
    }
}

int other(struct full_command* new_command)
{
    //executes a non-builtin command and returns the exit status of the process
    //if a background process is ran the function returns -1. 

    //create array for execvp argument in the following form:
    //newargv = [command, arguments, NULL]
    char *newargv[new_command->num_args+2];

    newargv[0] = new_command->command;
    for (int i = 0; i < new_command->num_args; i++)
    {
        newargv[i+1] = new_command->args[i];
    }
    newargv[new_command->num_args + 1] = NULL;

    //from the Process API exploration
	int child_status;

	// Fork a new process
	pid_t spawnPid = fork();

    //struct sigaction to set SIGINT to default for child (has to be declared outside switch) 
    struct sigaction SIGINT_action;
    memset(&SIGINT_action, 0, sizeof(struct sigaction));

    //struct sigaction to set SIGTSTP to ignore for child 
    struct sigaction SIGTSTP_action;
    memset(&SIGTSTP_action, 0, sizeof(struct sigaction));

	switch(spawnPid)
    {
        case -1:
            perror("Error");
            fflush(stdout);
            exit(1);
            break;
        case 0:                                                                 
        // Child process   
            
            //If child process is in foreground, we don't want it to ignore SIGINT
            if(new_command->bg == 0)
            {
                SIGINT_action.sa_handler = SIG_DFL;
                sigaction(SIGINT, &SIGINT_action, NULL);
            }

            //all children should ignore SIGTSTP
	        SIGTSTP_action.sa_handler = SIG_IGN;
	        sigaction(SIGTSTP, &SIGTSTP_action, NULL);

            //If an input file has been provided, redirect input
            if(new_command->input_file != NULL)
            {
                redirect_input(new_command->input_file);
            }
            //No input file provided, background process redirected to /dev/null
            else if (new_command->bg == 1)
            {
                redirect_input("/dev/null");
            }

            //If an output file has been provided, redirect output
            if (new_command->output_file != NULL)
            {
                redirect_output(new_command->output_file);
            }
            //No output file provided, background process redirected to /dev/null
            else if (new_command->bg == 1)
            {
                redirect_output("/dev/null");
            }

            // Replace the current program with "PATH/command"
            execvp(newargv[0], newargv);

            // exec only returns if there is an error
            perror(new_command->command);
            fflush(stdout);
            exit(1);
            break;

        default:        
        //Parent process
            if(new_command->bg == 0)
            {
            //foreground
                // Wait for child's termination
                spawnPid = waitpid(spawnPid, &child_status, 0);

                //If process is terminated we report the status
                if(WIFSIGNALED(child_status))
                {
                    status(child_status);
                }

                return child_status;
            }
            else
            {
            //background
                printf("Background pid is %d\n", spawnPid);
                fflush(stdout);

                //since the status has not been changed we return -1
                return -1;
            }
    }
}

void handle_SIGCHLD(int signo)
{
    pid_t pid;
    int child_status;

    pid = waitpid(-1, &child_status, WNOHANG);

    if(pid != -1 && pid != 0)
    {
        char* message = calloc(60, sizeof(char));
        if(WIFEXITED(child_status))
        {
            //Process finishes normally
            sprintf(message, "Background pid %d is done. Exit value %d\n:", pid, WEXITSTATUS(child_status));
            write(STDOUT_FILENO, message, 60);
            fflush(stdout);
	    } 
        else
        {
            //Process is terminated
            sprintf(message, "Background pid %d is done. Terminated by signal %d\n", pid, WTERMSIG(child_status));
            write(STDOUT_FILENO, message, 60);
            fflush(stdout);
	    }
    }
}

void handle_SIGTSTP(int signo)
{
    if (fg_mode == 0)
    {
        char* message = "\nEntering foreground-only mode (& is now ignored)\n: ";
        write(STDOUT_FILENO, message, 52);
        fflush(stdout);
        fg_mode = 1;
    }
    else if (fg_mode == 1)
    {
        char* message = "\nExiting foreground-only mode\n: ";
        write(STDOUT_FILENO, message, 32);
        fflush(stdout);
        fg_mode = 0;
    }
}

int main(void) 
{
    int child_status = 0;

    //Ignore SIGINT
	struct sigaction SIGINT_action;
    memset(&SIGINT_action, 0, sizeof(struct sigaction));
	SIGINT_action.sa_handler = SIG_IGN;
	sigaction(SIGINT, &SIGINT_action, NULL);
	fflush(stdout);

    //Signal Handler for SIGTSTP - From Signal Handling API Exploration
  	struct sigaction SIGTSTP_action;
    memset(&SIGTSTP_action, 0, sizeof(struct sigaction));
	SIGTSTP_action.sa_handler = handle_SIGTSTP;
	sigfillset(&SIGTSTP_action.sa_mask);
	SIGTSTP_action.sa_flags = SA_RESTART;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);
	fflush(stdout);

    //Signal Handler for background processes - From Signal Handling API Exploration
	struct sigaction SIGCHLD_action;
    memset(&SIGCHLD_action, 0, sizeof(struct sigaction));
	SIGCHLD_action.sa_handler = handle_SIGCHLD;
	sigfillset(&SIGCHLD_action.sa_mask);
	SIGCHLD_action.sa_flags = SA_RESTART;
	sigaction(SIGCHLD, &SIGCHLD_action, NULL);
	fflush(stdout);

    char *input = calloc(2048, sizeof(char));
    
    while(1)
    {
        input = getCommand();

        //Handler for comment
        if(strncmp(input, "#", 1) == 0)
        {
            continue;
        }

        //Check for empty input
        int empty = 1;
        for(int i = 0; i < strlen(input); i++)
        {
            if(*(input) != ' ')
            {
                empty = 0;
            }
        }

        if(empty)
        {
            continue;
        }

        //Handler for exit
        if(strcmp(input, "exit") == 0)
        {
            exit(0);
        }

        struct full_command *new_command = parseCommand(input);

        //handler for cd command
        if(strcmp(new_command->command, "cd") == 0 )
        {
            cd(new_command->args[0]);
        }
        //handler for status command
        else if(strcmp(new_command->command, "status") == 0)
        {
            status(child_status);
        }
        //handler for non-built-in command
        else
        {
            //We only change child_status if other() has executed a fg process and returned the exit status
            //If it ran a bg process it returns -1 
            int temp_status = other(new_command);
            if(temp_status != -1)
            {
                child_status = temp_status;
            }
        }

        free(new_command);
    }
    return 0;
}