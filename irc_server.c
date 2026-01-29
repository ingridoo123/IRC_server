#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <ctype.h>

#define PORT 8080
#define BUFFER_SIZE 2048
#define MAX_CLIENTS 100
#define MAX_CHANNELS 50
#define MAX_NICKNAME 32
#define MAX_CHANNEL_NAME 32
#define MAX_MESSAGE 512

typedef struct {
    int socket_fd;
    char nickname[MAX_NICKNAME];
    struct sockaddr_in addr;
    int authenticated;
} User;

typedef struct {
    char name[MAX_CHANNEL_NAME];
    User* members[MAX_CLIENTS];
    int member_count;
} Channel;

User* clients[MAX_CLIENTS];
int client_count = 0;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

Channel* channels[MAX_CHANNELS];
int channel_count = 0;
pthread_mutex_t channels_mutex = PTHREAD_MUTEX_INITIALIZER;

void* handle_client(void* arg);
void process_command(User* user, char* command);
void send_to_client(int socket_fd, const char* message);
void broadcast_to_channel(Channel* channel, const char* message, User* sender);
User* find_user_by_nickname(const char* nickname);
Channel* find_channel_by_name(const char* name);
Channel* create_channel(const char* name);
void add_user_to_channel(Channel* channel, User* user);
void remove_user_from_channel(Channel* channel, User* user);
void remove_client(User* user);
void trim_newline(char* str);
void send_help(int socket_fd);

int main() {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len;
    pthread_t thread_id;
    
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }
    
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }
    
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }
    
    printf("╔════════════════════════════════════════════════════════╗\n");
    printf("║         SERWER IRC        ║\n");
    printf("╚════════════════════════════════════════════════════════╝\n");
    printf("[*] Serwer nasluchuje na porcie %d\n", PORT);
    printf("[*] Oczekiwanie na polaczenia...\n\n");

    (void)create_channel("#general");

    while(1) {
        addr_len = sizeof(client_addr);
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        
        if (client_fd < 0) {
            perror("accept");
            continue;
        }
        
        printf("[+] Nowe polaczenie od: %s:%d (fd=%d)\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port),
               client_fd);
        
        User* user = malloc(sizeof(User));
        user->socket_fd = client_fd;
        user->addr = client_addr;
        user->authenticated = 0;
        strcpy(user->nickname, "");
        
        pthread_mutex_lock(&clients_mutex);
        if (client_count < MAX_CLIENTS) {
            clients[client_count++] = user;
        }
        pthread_mutex_unlock(&clients_mutex);
        
        send_to_client(client_fd, "╔════════════════════════════════════════════════════════╗\n");
        send_to_client(client_fd, "║          Witaj na serwerze IRC!                       ║\n");
        send_to_client(client_fd, "╚════════════════════════════════════════════════════════╝\n");
        send_to_client(client_fd, "Wpisz: HELP aby zobaczyc dostepne komendy\n");
        send_to_client(client_fd, "Zacznij od: NICK <twoj_pseudonim>\n\n");
        
        if (pthread_create(&thread_id, NULL, handle_client, user) != 0) {
            perror("pthread_create");
            close(client_fd);
            free(user);
            continue;
        }
        
        pthread_detach(thread_id);
    }
    
    close(server_fd);
    return 0;
}

void* handle_client(void* arg) {
    User* user = (User*)arg;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    
    printf("[Watek %lu] Obsluga klienta fd=%d\n", pthread_self(), user->socket_fd);
    
    while(1) {
        memset(buffer, 0, BUFFER_SIZE);
        bytes_read = read(user->socket_fd, buffer, BUFFER_SIZE - 1);
        
        if (bytes_read <= 0) {
            printf("[-] Klient %s (fd=%d) rozlaczony\n", 
                   user->nickname[0] ? user->nickname : "UNKNOWN",
                   user->socket_fd);
            break;
        }
        
        trim_newline(buffer);
        
        if (strlen(buffer) > 0) {
            printf("[%s] Komenda: %s\n", 
                   user->nickname[0] ? user->nickname : "ANONYMOUS", 
                   buffer);
            process_command(user, buffer);
        }
    }
    
    remove_client(user);
    close(user->socket_fd);
    free(user);
    pthread_exit(NULL);
}

void process_command(User* user, char* command) {
    char cmd[BUFFER_SIZE];
    char arg1[BUFFER_SIZE];
    char arg2[BUFFER_SIZE];
    
    memset(cmd, 0, BUFFER_SIZE);
    memset(arg1, 0, BUFFER_SIZE);
    memset(arg2, 0, BUFFER_SIZE);
    
    sscanf(command, "%s %s %[^\n]", cmd, arg1, arg2);
    
    for(int i = 0; cmd[i]; i++) {
        cmd[i] = toupper(cmd[i]);
    }
    
    if (strcmp(cmd, "HELP") == 0) {
        send_help(user->socket_fd);
    }
    
    else if (strcmp(cmd, "NICK") == 0) {
        if (strlen(arg1) == 0) {
            send_to_client(user->socket_fd, "ERROR Uzycie: NICK <pseudonim>\n");
            return;
        }
        
        pthread_mutex_lock(&clients_mutex);
        User* existing = find_user_by_nickname(arg1);
        if (existing != NULL && existing != user) {
            pthread_mutex_unlock(&clients_mutex);
            send_to_client(user->socket_fd, "ERROR Pseudonim zajety\n");
            return;
        }
        
        strcpy(user->nickname, arg1);
        user->authenticated = 1;
        pthread_mutex_unlock(&clients_mutex);
        
        char response[BUFFER_SIZE];
        snprintf(response, BUFFER_SIZE, "OK NICK %s\n", user->nickname);
        send_to_client(user->socket_fd, response);
        
        printf("[*] Uzytkownik ustawil nick: %s\n", user->nickname);
    }
    
    else if (strcmp(cmd, "JOIN") == 0) {
        if (!user->authenticated) {
            send_to_client(user->socket_fd, "ERROR Najpierw uzyj: NICK <pseudonim>\n");
            return;
        }
        
        if (strlen(arg1) == 0 || arg1[0] != '#') {
            send_to_client(user->socket_fd, "ERROR Uzycie: JOIN <#kanal>\n");
            return;
        }
        
        pthread_mutex_lock(&channels_mutex);
        
        Channel* channel = find_channel_by_name(arg1);
        if (channel == NULL) {
            channel = create_channel(arg1);
        }
        
        add_user_to_channel(channel, user);
        pthread_mutex_unlock(&channels_mutex);
        
        char response[BUFFER_SIZE];
        snprintf(response, BUFFER_SIZE, "OK JOIN %s\n", arg1);
        send_to_client(user->socket_fd, response);
        
        snprintf(response, BUFFER_SIZE, "USERJOINED %s %s\n", arg1, user->nickname);
        broadcast_to_channel(channel, response, user);
        
        printf("[*] %s dolaczyl do %s\n", user->nickname, arg1);
    }
    
    else if (strcmp(cmd, "LEAVE") == 0) {
        if (!user->authenticated) {
            send_to_client(user->socket_fd, "ERROR Najpierw uzyj: NICK <pseudonim>\n");
            return;
        }
        
        if (strlen(arg1) == 0) {
            send_to_client(user->socket_fd, "ERROR Uzycie: LEAVE <#kanal>\n");
            return;
        }
        
        pthread_mutex_lock(&channels_mutex);
        Channel* channel = find_channel_by_name(arg1);
        if (channel != NULL) {
            remove_user_from_channel(channel, user);
            
            char response[BUFFER_SIZE];
            snprintf(response, BUFFER_SIZE, "OK LEAVE %s\n", arg1);
            send_to_client(user->socket_fd, response);
            
            snprintf(response, BUFFER_SIZE, "USERLEFT %s %s\n", arg1, user->nickname);
            broadcast_to_channel(channel, response, NULL);
            
            printf("[*] %s opuscil %s\n", user->nickname, arg1);
        }
        pthread_mutex_unlock(&channels_mutex);
    }
    
    else if (strcmp(cmd, "MSG") == 0) {
    if (!user->authenticated) {
        send_to_client(user->socket_fd, "ERROR Najpierw uzyj: NICK <pseudonim>\n");
        return;
    }
    
    if (strlen(arg1) == 0 || strlen(arg2) == 0) {
        send_to_client(user->socket_fd, "ERROR Uzycie: MSG <#kanal> <wiadomosc>\n");
        return;
    }
    
    pthread_mutex_lock(&channels_mutex);
    Channel* channel = find_channel_by_name(arg1);
    if (channel != NULL) {
        int is_member = 0;
        for (int i = 0; i < channel->member_count; i++) {
            if (channel->members[i] == user) {
                is_member = 1;
                break;
            }
        }
        
        if (!is_member) {
            pthread_mutex_unlock(&channels_mutex);
            send_to_client(user->socket_fd, "ERROR Nie jestes na tym kanale! Uzyj: JOIN ");
            send_to_client(user->socket_fd, arg1);
            send_to_client(user->socket_fd, "\n");
            return;
        }
        
        char message[BUFFER_SIZE];
        snprintf(message, BUFFER_SIZE, "MESSAGE %s %s %s\n", arg1, user->nickname, arg2);
        broadcast_to_channel(channel, message, user);
        
        printf("[MSG %s] %s: %s\n", arg1, user->nickname, arg2);
    } else {
        send_to_client(user->socket_fd, "ERROR Kanal nie istnieje\n");
    }
    pthread_mutex_unlock(&channels_mutex);
   }
    
    else if (strcmp(cmd, "PRIVMSG") == 0) {
        if (!user->authenticated) {
            send_to_client(user->socket_fd, "ERROR Najpierw uzyj: NICK <pseudonim>\n");
            return;
        }
        
        if (strlen(arg1) == 0 || strlen(arg2) == 0) {
            send_to_client(user->socket_fd, "ERROR Uzycie: PRIVMSG <nick> <wiadomosc>\n");
            return;
        }
        
        pthread_mutex_lock(&clients_mutex);
        User* recipient = find_user_by_nickname(arg1);
        if (recipient != NULL) {
            char message[BUFFER_SIZE];
            snprintf(message, BUFFER_SIZE, "PRIVMSG %s %s\n", user->nickname, arg2);
            send_to_client(recipient->socket_fd, message);
            send_to_client(user->socket_fd, "OK PRIVMSG\n");
            
            printf("[PRIV] %s -> %s: %s\n", user->nickname, arg1, arg2);
        } else {
            send_to_client(user->socket_fd, "ERROR Uzytkownik nie znaleziony\n");
        }
        pthread_mutex_unlock(&clients_mutex);
    }
    
    else if (strcmp(cmd, "LIST") == 0) {
        if (!user->authenticated) {
            send_to_client(user->socket_fd, "ERROR Najpierw uzyj: NICK <pseudonim>\n");
            return;
        }
        
        pthread_mutex_lock(&channels_mutex);
        char response[BUFFER_SIZE];
        strcpy(response, "CHANNELLIST ");
        
        for (int i = 0; i < channel_count; i++) {
            strcat(response, channels[i]->name);
            if (i < channel_count - 1) {
                strcat(response, ",");
            }
        }
        strcat(response, "\n");
        pthread_mutex_unlock(&channels_mutex);
        
        send_to_client(user->socket_fd, response);
    }
    
    else if (strcmp(cmd, "USERS") == 0) {
        if (!user->authenticated) {
            send_to_client(user->socket_fd, "ERROR Najpierw uzyj: NICK <pseudonim>\n");
            return;
        }
        
        if (strlen(arg1) == 0) {
            send_to_client(user->socket_fd, "ERROR Uzycie: USERS <#kanal>\n");
            return;
        }
        
        pthread_mutex_lock(&channels_mutex);
        Channel* channel = find_channel_by_name(arg1);
        if (channel != NULL) {
            char response[BUFFER_SIZE];
            strcpy(response, "USERLIST ");
            
            for (int i = 0; i < channel->member_count; i++) {
                strcat(response, channel->members[i]->nickname);
                if (i < channel->member_count - 1) {
                    strcat(response, ",");
                }
            }
            strcat(response, "\n");
            send_to_client(user->socket_fd, response);
        } else {
            send_to_client(user->socket_fd, "ERROR Kanal nie istnieje\n");
        }
        pthread_mutex_unlock(&channels_mutex);
    }
    
    else if (strcmp(cmd, "QUIT") == 0) {
        send_to_client(user->socket_fd, "OK QUIT - Do zobaczenia!\n");
        close(user->socket_fd);
        pthread_exit(NULL);
    }
    
    else {
        send_to_client(user->socket_fd, "ERROR Nieznana komenda. Wpisz HELP aby zobaczyc dostepne komendy.\n");
    }
}

void send_help(int socket_fd) {
    send_to_client(socket_fd, "\n");
    send_to_client(socket_fd, "╔════════════════════════════════════════════════════════════════╗\n");
    send_to_client(socket_fd, "║                    DOSTEPNE KOMENDY IRC                       ║\n");
    send_to_client(socket_fd, "╚════════════════════════════════════════════════════════════════╝\n");
    send_to_client(socket_fd, "\n");
    send_to_client(socket_fd, "  NICK <pseudonim>          - Ustaw swoj pseudonim\n");
    send_to_client(socket_fd, "                              Przyklad: NICK JanKowalski\n");
    send_to_client(socket_fd, "\n");
    send_to_client(socket_fd, "  JOIN <#kanal>             - Dolacz do kanalu (lub stworz nowy)\n");
    send_to_client(socket_fd, "                              Przyklad: JOIN #general\n");
    send_to_client(socket_fd, "\n");
    send_to_client(socket_fd, "  LEAVE <#kanal>            - Opusc kanal\n");
    send_to_client(socket_fd, "                              Przyklad: LEAVE #general\n");
    send_to_client(socket_fd, "\n");
    send_to_client(socket_fd, "  MSG <#kanal> <wiadomosc>  - Wyslij wiadomosc na kanal\n");
    send_to_client(socket_fd, "                              Przyklad: MSG #general Witam!\n");
    send_to_client(socket_fd, "\n");
    send_to_client(socket_fd, "  PRIVMSG <nick> <tekst>    - Wyslij prywatna wiadomosc\n");
    send_to_client(socket_fd, "                              Przyklad: PRIVMSG Anna Czesc!\n");
    send_to_client(socket_fd, "\n");
    send_to_client(socket_fd, "  LIST                      - Wyswietl liste kanalow\n");
    send_to_client(socket_fd, "\n");
    send_to_client(socket_fd, "  USERS <#kanal>            - Wyswietl uzytkownikow na kanale\n");
    send_to_client(socket_fd, "                              Przyklad: USERS #general\n");
    send_to_client(socket_fd, "\n");
    send_to_client(socket_fd, "  HELP                      - Wyswietl ta pomoc\n");
    send_to_client(socket_fd, "\n");
    send_to_client(socket_fd, "  QUIT                      - Rozlacz sie z serwerem\n");
    send_to_client(socket_fd, "\n");
    send_to_client(socket_fd, "╚════════════════════════════════════════════════════════════════╝\n");
    send_to_client(socket_fd, "\n");
}

void send_to_client(int socket_fd, const char* message) {
    write(socket_fd, message, strlen(message));
}

void broadcast_to_channel(Channel* channel, const char* message, User* sender) {
    for (int i = 0; i < channel->member_count; i++) {
        if (sender != NULL && channel->members[i] == sender) {
            continue;
        }
        send_to_client(channel->members[i]->socket_fd, message);
    }
}

User* find_user_by_nickname(const char* nickname) {
    for (int i = 0; i < client_count; i++) {
        if (strcmp(clients[i]->nickname, nickname) == 0) {
            return clients[i];
        }
    }
    return NULL;
}

Channel* find_channel_by_name(const char* name) {
    for (int i = 0; i < channel_count; i++) {
        if (strcmp(channels[i]->name, name) == 0) {
            return channels[i];
        }
    }
    return NULL;
}

Channel* create_channel(const char* name) {
    if (channel_count >= MAX_CHANNELS) {
        return NULL;
    }
    
    Channel* channel = malloc(sizeof(Channel));
    strcpy(channel->name, name);
    channel->member_count = 0;
    
    channels[channel_count++] = channel;
    printf("[*] Utworzono nowy kanal: %s\n", name);
    
    return channel;
}

void add_user_to_channel(Channel* channel, User* user) {
    for (int i = 0; i < channel->member_count; i++) {
        if (channel->members[i] == user) {
            return;
        }
    }
    
    if (channel->member_count < MAX_CLIENTS) {
        channel->members[channel->member_count++] = user;
    }
}

void remove_user_from_channel(Channel* channel, User* user) {
    for (int i = 0; i < channel->member_count; i++) {
        if (channel->members[i] == user) {
            for (int j = i; j < channel->member_count - 1; j++) {
                channel->members[j] = channel->members[j + 1];
            }
            channel->member_count--;
            break;
        }
    }
}

void remove_client(User* user) {
    pthread_mutex_lock(&channels_mutex);
    for (int i = 0; i < channel_count; i++) {
        remove_user_from_channel(channels[i], user);
    }
    pthread_mutex_unlock(&channels_mutex);
    
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < client_count; i++) {
        if (clients[i] == user) {
            for (int j = i; j < client_count - 1; j++) {
                clients[j] = clients[j + 1];
            }
            client_count--;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

void trim_newline(char* str) {
    int len = strlen(str);
    while (len > 0 && (str[len-1] == '\n' || str[len-1] == '\r')) {
        str[len-1] = '\0';
        len--;
    }
}
