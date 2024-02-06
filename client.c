#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <signal.h>
#include <sys/select.h>
#include <stdbool.h>

#define PORT 2909
#define MAX_BUFF 1024
extern int errno;

struct timeval timeout;
fd_set active_fds;
struct sockaddr_in server;
int sd;
char buff[MAX_BUFF];
bool exited = false;

void setupCl();
void connectCl();
void loginClient(char *nickname);
void game();

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Please enter a username!\n./client [username]\n");
        exit(1);
    }

    setupCl();
    connectCl();

    memset(buff, 0, MAX_BUFF - 1);
    if (read(sd, buff, MAX_BUFF - 1) < 0)
    {
        perror("[client] Error at receiving message from server \n");
        exit(1);
    }
    buff[strlen(buff)] = '\0';
    printf("%s", buff);
    fflush(stdout);

    while (1)
    {
        memset(buff, 0, MAX_BUFF - 1);
        strcpy(buff, argv[1]); // sending the username
        loginClient(buff);

        memset(buff, 0, MAX_BUFF - 1);

        int connected;
        if (read(sd, &connected, sizeof(int)) < 0)
        {
            perror("[client|login] Error at reading from the server.\n");
            return errno;
        }
        if (connected != 1)
        {
            system("clear");
            printf("Wrong USERNAME or PASSWORD, please try again!\n");
            fflush(stdout);
        }
        else
        {
            system("clear");
            printf("Login successful!\n");
            fflush(stdout);
            break;
        }
    } // login while

    game();
    close(sd);
} // main

void game()
{
    int q_id = 1;

    system("clear");
    printf("QUIZZGAME STARTS NOW!\nPlease wait for others to connect!\n");
    sleep(1);

    while (q_id < 11)
    {
        // system("clear");
        memset(buff, 0, MAX_BUFF - 1);
        if (read(sd, &buff, MAX_BUFF - 1) <= 0)
        {
            perror("[client] Error at read() the question from the server.\n");
            exit(1);
        }
        printf("The question number %d and the answers are:\n%s", q_id, buff);
        fflush(stdout);
        q_id++;

        FD_ZERO(&active_fds);
        FD_SET(0, &active_fds);
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        int result = select(1, &active_fds, NULL, NULL, &timeout);
        if (result < 0)
        {
            perror("Error at select!\n");
            exit(1);
        }
        else if (result > 0)
        {
        loop:
            memset(buff, 0, MAX_BUFF - 1);
            if (fgets(buff, sizeof(buff), stdin) == NULL)
            {
                perror("[client] Error at reading user input.\n");
            }
            buff[strcspn(buff, "\n")] = '\0';
            if (write(sd, buff, MAX_BUFF - 1) <= 0)
            {
                perror("[client] Error at writing the answer towards the server.\n");
                exit(1);
            }

            if (strcmp(buff, "exit") == 0)
            {
                exited = true;
                printf("You chose to exit the quizzgame!\n");
                fflush(stdout);
                break;
            }
            else
            {
                // daca am introdus mesaj gresit pot sa mai introduc in limita timpului
                if (strcmp(buff, "a") != 0 && strcmp(buff, "b") != 0 && strcmp(buff, "c") != 0 && strcmp(buff, "d") != 0 && strcmp(buff, "") != 0)
                {
                    memset(buff, 0, MAX_BUFF - 1);
                    if (read(sd, &buff, MAX_BUFF - 1) <= 0)
                    {
                        perror("[client] Error at reading the validation and score from the server.\n");
                        exit(1);
                    }
                    printf("%s", buff);
                    fflush(stdout);
                    goto loop;
                }
                // primeste validatea raspunsului
                memset(buff, 0, MAX_BUFF - 1);
                if (read(sd, &buff, MAX_BUFF - 1) <= 0)
                {
                    perror("[client] Error at reading the validation and score from the server.\n");
                    exit(1);
                }
                printf("%s", buff);
                fflush(stdout);
            }
            // mesaj de wait
            memset(buff, 0, sizeof(buff));
            if (read(sd, &buff, sizeof(buff)) <= 0)
            {
                perror("[client] Error at reading the wait message from the server.\n");
                exit(1);
            }
            printf("%s", buff);
            fflush(stdout);
        }
        else // time expired
        {
            memset(buff, 0, MAX_BUFF - 1);
            strcpy(buff, "nothing");
            if (write(sd, buff, strlen(buff)) <= 0)
            {
                perror("[client] Error at writing the answer towards the server.\n");
                exit(1);
            }
            memset(buff, 0, MAX_BUFF - 1);
            if (read(sd, &buff, MAX_BUFF - 1) <= 0)
            {
                perror("[client] Error at reading the validation and score from the server.\n");
                exit(1);
            }
            printf("%s", buff);
            fflush(stdout);
        }
    } // while sunt intrebari

    system("clear");
    printf("The quizzgame ended!\n");
    fflush(stdout);
    memset(buff, 0, MAX_BUFF - 1);
    if (read(sd, &buff, MAX_BUFF - 1) < 0)
    {
        perror("[client] Errormessage at reading the winner message from the server.\n");
        exit(1);
    }
    printf("%s\n", buff);
    fflush(stdout);
}

void setupCl()
{
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr("127.0.0.1");
    server.sin_port = htons(PORT);
}
void connectCl()
{
    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("[client] Error at creating the socket\n");
        exit(1);
    }
    if (connect(sd, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1)
    {
        perror("[client] Error at connecting to the server  \n");
        exit(1);
    }
}
void loginClient(char *buffer)
{
    if (write(sd, buffer, strlen(buffer)) < 0)
    {
        perror("[client] Error at sending nickname to the server  \n");
        exit(1);
    }

    buffer[0] = '\0';

    if (fgets(buffer, MAX_BUFF - 1, stdin) == NULL)
    {
        perror("[client] Error reading user input");
        exit(1);
    }
    buffer[strcspn(buffer, "\n")] = '\0';

    if (write(sd, buffer, strlen(buffer)) < 0)
    {
        perror("[client] Error at sending chosen password to the server  \n");
        exit(1);
    }
}