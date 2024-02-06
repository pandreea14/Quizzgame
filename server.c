#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <pthread.h>

#define PORT 2909
#define MAX_CLIENTS 100
#define MAX_BUFF 1024
extern int errno;

struct clients
{
    char username[50];
    char password[50];
    int score;
    int cld;
} clients[MAX_CLIENTS];

typedef struct Thread
{
    int idThread;
    int cl;
} Thread;
pthread_t th[100];

struct sockaddr_in server;
struct sockaddr_in from;
int count_clients;
int sd;
int total_players;
int q_id;
bool exited;
int new_game = 1;
int threads_at_barrier;
char given_answ[MAX_BUFF];
char message[MAX_BUFF];
static bool isDatabaseOpen = false;
sqlite3 *db;
sqlite3_stmt *res;
char *msgErr;
pthread_mutex_t mlock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t cond_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t dbMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void openDatabase();
void deleteTableUsers()
{
    openDatabase();
    pthread_mutex_lock(&dbMutex);
    char *sql = "DROP TABLE users;";
    int rc = sqlite3_exec(db, sql, 0, 0, 0);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "[deleteUsers] Failed to execute statement: %s\n", sqlite3_errmsg(db));
        isDatabaseOpen = false;
        sqlite3_close(db);
    }
    printf("[deleteUsers] Table users dropped!\n");
    char *command = "CREATE TABLE IF NOT EXISTS users(id INTEGER, username TEXT PRIMARY KEY, password TEXT);";
    int result = sqlite3_exec(db, command, 0, 0, 0);
    if (result != SQLITE_OK)
    {
        fprintf(stderr, "[createUsers] Failed to execute statement: %s\n", sqlite3_errmsg(db));
        isDatabaseOpen = false;
        sqlite3_close(db);
    }
    printf("[createUsers] Table users created!\n");
    isDatabaseOpen = false;
    sqlite3_close(db);
    isDatabaseOpen = false;
    pthread_mutex_unlock(&dbMutex);
    sqlite3_close(db);
    pthread_mutex_destroy(&dbMutex);
}
void ctrlC(int sig)
{
    char Z;
    signal(sig, SIG_IGN);
    printf("\nARE YOU SURE YOU WANT TO CLOSE THE SERVER??? [Y/N]\n");
    Z = getchar();
    if (Z == 'y' || Z == 'Y')
    {
        deleteTableUsers();
        exit(0);
    }
    else
    {
        signal(SIGINT, ctrlC);
    }
    getchar();
}
void serverSetup();
void createSocket();
void extractAll(int id, char *buffer);
void extractCorrectAnswer(int id, char *answer);
static void *treat(void *arg);
void login(struct Thread thPlayer);
int newUser(int idThread);
int addUser(int idThread);
int verifyPassword(int idThread);
void initPlayerInfo(int cl, int idThread);
void sendQuestionandOptions(int cl, int idThread, int q_id);
void receiveAnswer(int cl, int idThread);
void sendValidation(int cl, int idThread, int q_id);
void sendWinnerMessage(int cl, int idThread);
void gameLogic(struct Thread thPlayer);

int main()
{
    signal(SIGINT, ctrlC);
    serverSetup();
    createSocket();

    printf("[server] Waiting for players to connect to port %d...\n", PORT);
    fflush(stdout);

    // servim in mod concurent clientii...folosind thread-uri
    while (1)
    {
        int len = sizeof(from), client;
        Thread *td;

        printf("newgame is %d\n", new_game);
        pthread_mutex_lock(&mlock); // to avoid race conitions
        if ((client = accept(sd, (struct sockaddr *)&from, (socklen_t *)&len)) < 0)
        {
            perror("[server] Accept failed.\n");
            continue;
        }
        pthread_mutex_unlock(&mlock);
        if (new_game == 1)
        {
            count_clients = 0;
            total_players = 0;
            exited = false;
            q_id = 1;
            new_game = 0;
        }
        count_clients++;

        memset(message, 0, MAX_BUFF - 1);
        strcpy(message, "Please login/register before starting the quizz.\nYou will have to answer to 10 questions for each you have 10 seconds to give the best answer.\nTry to do it faster than your opponents.\nGood luck!\n");
        if (write(client, message, strlen(message)) <= 0)
        {
            perror("[server] Writing message to client failed.\n");
            exit(1);
        }
        printf("[server] Connection with a client of descriptor %d.\n", client);
        fflush(stdout);

        // s-a realizat conexiunea, se creaza threadul
        td = (struct Thread *)malloc(sizeof(struct Thread));
        td->idThread = count_clients;
        td->cl = client;
        if (pthread_create(&th[count_clients], NULL, &treat, td) != 0)
        {
            perror("[server] Thread creation failed.\n");
            continue;
        }
    }
    return 0;
} // main

void serverSetup()
{
    memset(&server, 0, sizeof(server));
    memset(&from, 0, sizeof(from));

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY); // e host
    server.sin_port = htons(PORT);              // portul
}
void createSocket()
{
    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("[server]Socket failed\n");
        exit(1);
    }
    int optval = 1;
    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    if (bind(sd, (struct sockaddr *)&server, sizeof(struct sockaddr)) == -1)
    {
        perror("[server]Bind failed\n");
        exit(1);
    }
    if (listen(sd, MAX_CLIENTS) == -1) // pot astepta MAX_CLIENTS = 100 clienti sa se conecteze
    {
        perror("[server]Listen failed\n");
        exit(1);
    }
}
void *treat(void *arg)
{
    struct Thread thPlayer;
    thPlayer = *((struct Thread *)arg);
    printf("[thread- %d] Waiting for players to login...\n", thPlayer.idThread);
    fflush(stdout);

    pthread_detach(pthread_self());

    login(thPlayer);
    gameLogic(thPlayer);

    close(thPlayer.cl);
    printf("\nOFICIAL AM TERMINAT CU ACEST CLIENT\n\n");
    return (NULL);
}

int final = 0;
void gameLogic(struct Thread thPlayer)
{
    total_players++; // va creste la fiecare client care ajunge in functia de joc SI NU SCADE NICIODATA
    printf("Total players: %d, and count clients: %d\n", total_players, count_clients);
    while (total_players < 2)
        sleep(1);

    threads_at_barrier = 0;
    while (q_id < 11)
    {
        printf("QUESTION %d for client %d\n", q_id, thPlayer.idThread);
        sendQuestionandOptions(thPlayer.cl, thPlayer.idThread, q_id);
        receiveAnswer(thPlayer.cl, thPlayer.idThread);

        threads_at_barrier++;
        if (strcmp(given_answ, "nothing") == 0)
        {

            memset(message, 0, MAX_BUFF - 1);
            snprintf(message, sizeof(message), "Your 10 seconds expired... no points for you :(\nScore = %d \n", clients[thPlayer.idThread].score);

            printf("in nimic.....count client e %d, total players e %d, threads at barrier e %d\n", count_clients, total_players, threads_at_barrier);
            if (write(thPlayer.cl, message, MAX_BUFF - 1) <= 0)
            {
                perror("[server] Writing wait message towards client failed.\n");
                exit(1);
            }
            if (threads_at_barrier % count_clients == 0)
            {
                pthread_mutex_lock(&cond_mutex);
                q_id++;
                pthread_cond_broadcast(&cond);
                pthread_mutex_unlock(&cond_mutex);
            }
        }
        else if (strcmp(given_answ, "exit") == 0)
        {
            // count_clients--;
            exited = true;

            printf("EXIT.....count client e %d, total players e %d, threads at barrier e %d\n", count_clients, total_players, threads_at_barrier);
            if (threads_at_barrier % count_clients == 0)
            {
                pthread_mutex_lock(&cond_mutex);
                q_id++;
                count_clients--;
                threads_at_barrier--;
                pthread_cond_broadcast(&cond);
                pthread_mutex_unlock(&cond_mutex);
            }
            printf("Player decided to exit game!\n");
            break;
        }
        else
        {
            printf("count client e %d\ntotal players e %d\nthreads at barrier e %d\n", count_clients, total_players, threads_at_barrier);
            sendValidation(thPlayer.cl, thPlayer.idThread, q_id);
            if (threads_at_barrier % count_clients == 0) // doar la ultimul client trecem mai departe
            {

                memset(message, 0, MAX_BUFF - 1);
                strcpy(message, "...\n");
                if (write(thPlayer.cl, message, MAX_BUFF - 1) <= 0)
                {
                    perror("[server] Writing wait message towards client failed.\n");
                    exit(1);
                }
                pthread_mutex_lock(&cond_mutex);
                q_id++;
                if (exited == true) // cazul cand dai exit inainte sa scrie toti, ultimul va scadea jucatorul iesit
                {
                    threads_at_barrier--;
                    count_clients--;
                    exited = false;
                    pthread_cond_broadcast(&cond);
                    pthread_mutex_unlock(&cond_mutex);
                    continue;
                }
                pthread_cond_broadcast(&cond);
                pthread_mutex_unlock(&cond_mutex);
            }
        }
        // wait clients
        if (threads_at_barrier % count_clients != 0) // daca nu esti ultimul => astepti pe restul
        {
            if (strcmp(given_answ, "nothing") != 0) // daca ai primit mesaj de al client => ii zici sa astepte
            {
                memset(message, 0, MAX_BUFF - 1);
                strcpy(message, "-------------Wait for the 10 seconds to end-------------\n------------or for the last player to finish------------\n");
                if (write(thPlayer.cl, message, MAX_BUFF - 1) <= 0)
                {
                    perror("[server] Writing wait message towards client failed.\n");
                    exit(1);
                }
            }
            pthread_mutex_lock(&cond_mutex);
            pthread_cond_wait(&cond, &cond_mutex);
            pthread_mutex_unlock(&cond_mutex);
        }
        exited = false;
    } // while sunt intrebari si jucatori

    printf("[thread %d] GATA JOCUL si exited e %d\n", thPlayer.idThread, exited);
    pthread_mutex_lock(&mutex);
    sendWinnerMessage(thPlayer.cl, thPlayer.idThread);
    pthread_mutex_unlock(&mutex);

    if (final == 1)
    {
        count_clients = 1;
        new_game = 1;
    }
}

void login(struct Thread thPlayer)
{
    int connected = 0;
    do
    {
        initPlayerInfo(thPlayer.cl, count_clients); // setat username si parola
        connected = newUser(thPlayer.idThread);
        if (connected == 1)
        {
            connected = addUser(thPlayer.idThread);
        }
        else if (connected == 0) // daca username ul exista deja verific daca e parola buna
        {
            connected = verifyPassword(thPlayer.idThread);
        }
        if (write(thPlayer.cl, &connected, sizeof(int)) <= 0)
        {
            printf("[Thread %d]\n", thPlayer.idThread);
            perror("[server] Writing message to client failed.\n");
        }
    } while (connected == 0);
    if (connected == 1)
    {
        printf("User has logged in!\n");
        return;
    }

    fprintf(stderr, "[login] Could not connect client!!\n");
}
int verifyPassword(int idThread)
{
    openDatabase();
    char command[MAX_BUFF];
    snprintf(command, sizeof(command), "SELECT password FROM users WHERE username='%s';",
             clients[idThread].username);

    int rc = sqlite3_prepare_v2(db, command, -1, &res, 0);

    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "[verifyPass] Failed to execute statement: %s\n", sqlite3_errmsg(db));
        return errno;
    }

    int step = sqlite3_step(res);
    if (step == SQLITE_ROW)
    {
        if (strcmp(clients[idThread].password, (const char *)sqlite3_column_text(res, 0)) == 0)
        {
            // printf("[verifyPass] User put correct password!\n");
            sqlite3_finalize(res);
            isDatabaseOpen = false;
            sqlite3_close(db);
            return 1;
        }
    }
    else
    {
        fprintf(stderr, "[verifyPass] Could not verify user!\n");
        fprintf(stderr, "Error: %s\n", msgErr);
        sqlite3_free(msgErr);
    }
    sqlite3_finalize(res);
    isDatabaseOpen = false;
    sqlite3_close(db);
    return 0;
}
int addUser(int idThread)
{
    openDatabase();
    char command[MAX_BUFF];
    snprintf(command, sizeof(command), "INSERT INTO users VALUES(%d, '%s', '%s');",
             idThread, clients[idThread].username, clients[idThread].password);

    int rc = sqlite3_exec(db, command, 0, 0, 0);

    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "[addUser] Failed to execute statement: %s\n", sqlite3_errmsg(db));
        isDatabaseOpen = false;
        sqlite3_close(db);
        return errno;
    }

    // printf("[addUser] User added to database!\n");
    isDatabaseOpen = false;
    sqlite3_close(db);

    return 1;
}
int newUser(int idThread)
{
    openDatabase();
    char command[MAX_BUFF];
    snprintf(command, sizeof(command), "SELECT * FROM users WHERE username LIKE '%s';",
             clients[idThread].username);

    int rc = sqlite3_prepare_v2(db, command, -1, &res, 0);

    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "[newUser] Failed to execute statement: %s\n", sqlite3_errmsg(db));
        return errno;
    }

    int step = sqlite3_step(res);
    if (step == SQLITE_ROW)
    {
        // printf("[newUser] User found in database!\n");
        sqlite3_finalize(res);
        isDatabaseOpen = false;
        sqlite3_close(db);
        return 0;
    }
    else if (step == SQLITE_DONE)
    {
        // printf("[newUser] Could not find user!\n");
        sqlite3_finalize(res);
        isDatabaseOpen = false;
        sqlite3_close(db);
        return 1;
    }
    fprintf(stderr, "Error executing query: %s\n", sqlite3_errmsg(db));
    sqlite3_finalize(res);
    isDatabaseOpen = false;
    sqlite3_close(db);
    return errno;
}

void initPlayerInfo(int cl, int idThread)
{
    memset(clients[idThread].username, 0, MAX_BUFF - 1);
    if (read(cl, &clients[idThread].username, 49) <= 0)
    {
        printf("[Thread %d]\n", idThread);
        perror("[server] Reading nickname from client failed.\n");
        exit(1);
    }

    clients[idThread].score = 0;
    clients[idThread].cld = cl;
    printf("[Thread %d] Hello %s!\n", idThread, clients[idThread].username);

    // memset(message, 0, MAX_BUFF);
    if (read(cl, &clients[idThread].password, 49) <= 0)
    {
        printf("[Thread %d]\n", idThread);
        perror("[server] Reading password from client failed.\n");
        exit(1);
    }
    printf("[Thread %d] Entered password: %s!\n", idThread, clients[idThread].password);
    fflush(stdout);
}
void sendQuestionandOptions(int cl, int idThread, int q_id)
{
    char buffer[MAX_BUFF];
    memset(buffer, 0, MAX_BUFF - 1);
    extractAll(q_id, buffer);
    strcat(buffer, "Enter a response(a, b, c or d) or 'exit' to quit: \n");
    if (write(cl, buffer, strlen(buffer)) <= 0)
    {
        perror("[Thread] Error at write() question towards client.\n");
    }

    printf("[Thread %d] Question was sent successfully.\n", idThread);
}
void receiveAnswer(int cl, int idThread)
{
    memset(given_answ, 0, MAX_BUFF - 1);
    if (read(cl, &given_answ, MAX_BUFF - 1) <= 0)
    {
        printf("[Thread %d]\n", idThread);
        perror("Error at read() answer from client.\n");
        printf("the 10 seconds expired... no points for this client :(\n");
    }

    printf("[Thread %d] Answer %s... was chosen successfully.\n", idThread, given_answ);
}
void sendValidation(int cl, int idThread, int q_id)
{
    char answer[MAX_BUFF];
    memset(answer, 0, MAX_BUFF - 1);
    extractCorrectAnswer(q_id, answer);

    if (strcmp(given_answ, "a") != 0 && strcmp(given_answ, "b") != 0 && strcmp(given_answ, "c") != 0 && strcmp(given_answ, "d") != 0 && strcmp(given_answ, "nothing") != 0)
    {
        memset(message, 0, MAX_BUFF - 1);
        strcpy(message, "Try typing a valid answer like a, b, c or d!\n");
        if (write(cl, message, strlen(message)) <= 0)
        {
            perror("[server] writing validation towards client failed.\n");
            exit(1);
        }
        receiveAnswer(cl, idThread);
    }
    if (strcmp(given_answ, answer) == 0)
    {
        clients[idThread].score++;
        memset(message, 0, MAX_BUFF - 1);
        snprintf(message, sizeof(message), "Correct answer !!!\nScore = %d \n", clients[idThread].score);
    }
    else
    {
        memset(message, 0, MAX_BUFF - 1);
        snprintf(message, sizeof(message), "Wrong answer :((\nScore = %d\n", clients[idThread].score);
    }

    if (write(cl, message, strlen(message)) <= 0)
    {
        perror("[server] Writing validation towards client failed.\n");
        exit(1);
    }
    memset(message, 0, MAX_BUFF - 1);
}
void sendWinnerMessage(int cl, int idThread)
{
    int max_score = clients[1].score;
    for (int ind = 2; ind <= total_players; ind++)
    {
        if (clients[ind].score > max_score)
        {
            max_score = clients[ind].score;
        }
    } // aflu valoarea scorului maxim

    printf("scorul clientului %d este %d\n", idThread, clients[idThread].score);
    printf("scorul maxim este %d\n", max_score);

    memset(message, 0, MAX_BUFF - 1);
    if (exited == false)
    {
        final = 1;
        if (clients[idThread].score == max_score)
        {
            snprintf(message, sizeof(message), "------------------CONGRATS------------------\n-----------You won with %d points!-----------\n----------------------:)---------------------", max_score);
        }
        else
        {
            snprintf(message, sizeof(message), "---------------------:(--------------------\n---------You lost with %d points :(---------\n----------The winner had %d points!----------", clients[idThread].score, max_score);
        }
    }
    else
    {
        snprintf(message, sizeof(message), "---------------------:(--------------------\n---------You exited the gaamee :(-----------\n---------------------------------------------");
    }

    // printf("--thread %d-- is here with message %s\n", idThread, message);
    // fflush(stdout);
    if (write(cl, message, strlen(message)) <= 0)
    {
        perror("[server] Writing winner message towards client failed.\n");
        exit(1);
    }
}

void openDatabase()
{
    if (isDatabaseOpen)
    {
        // fprintf(stderr, "[server|db] Database already opened!\n");
        return;
    }
    if (sqlite3_open("quizzgame.db", &db) != SQLITE_OK)
    {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
    isDatabaseOpen = true;
    // fprintf(stderr, "[server|db] Database successfully opened!\n");
}
void extractAll(int id, char *buffer)
{
    openDatabase();
    pthread_mutex_lock(&dbMutex);

    char *sql = "SELECT question, answ1, answ2, answ3, answ4 FROM Questions WHERE id = ?;";

    int rc = sqlite3_prepare_v2(db, sql, -1, &res, 0);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "[extractQUESTION] Failed to execute statement: %s\n", sqlite3_errmsg(db));
        exit(1);
    }
    sqlite3_bind_int(res, 1, id);

    int step = sqlite3_step(res);
    if (step == SQLITE_ROW)
    {
        strcat(buffer, (char *)sqlite3_column_text(res, 0));
        strcat(buffer, "\na).");
        strcat(buffer, (char *)sqlite3_column_text(res, 1));
        strcat(buffer, "\nb).");
        strcat(buffer, (char *)sqlite3_column_text(res, 2));
        strcat(buffer, "\nc).");
        strcat(buffer, (char *)sqlite3_column_text(res, 3));
        strcat(buffer, "\nd).");
        strcat(buffer, (char *)sqlite3_column_text(res, 4));
        strcat(buffer, "\n");
    }
    else
    {
        fprintf(stderr, "Could not extract anything!\n");
        fprintf(stderr, "Error: %s\n", msgErr);
        sqlite3_free(msgErr);
    }

    sqlite3_finalize(res);
    pthread_mutex_unlock(&dbMutex);
}
void extractCorrectAnswer(int q_id, char *answer)
{
    openDatabase();
    pthread_mutex_lock(&dbMutex);

    char *sql = "SELECT answ_c FROM Questions WHERE id = ?;";

    int result = sqlite3_prepare_v2(db, sql, -1, &res, 0);
    if (result != SQLITE_OK)
    {
        fprintf(stderr, "[extractCORRECTansw] Failed to execute statement: %s\n", sqlite3_errmsg(db));
        exit(1);
    }
    sqlite3_bind_int(res, 1, q_id);

    int st = sqlite3_step(res);

    if (st == SQLITE_ROW)
    {
        strcat(answer, (char *)sqlite3_column_text(res, 0));
        // printf("Correct answers successfully extracted!\n");
    }
    else
    {
        fprintf(stderr, "Could not extract answers!\n");
        fprintf(stderr, "Error: %s\n", msgErr);
        sqlite3_free(msgErr);
    }

    sqlite3_finalize(res);
    pthread_mutex_unlock(&dbMutex);
}