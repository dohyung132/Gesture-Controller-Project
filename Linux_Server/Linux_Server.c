#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <mysql/mysql.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <errno.h>

// ==============================================================================
// 시스템 설정값 매크로
// ==============================================================================
#define PORT 8080
#define DB_HOST "localhost"
#define DB_USER "root"
#define DB_PASS "iot"
#define DB_NAME "gesture_db"
#define ARDUINO_PORT "/dev/ttyACM0"
#define MAX_CLIENTS 10

#define COLOR_RESET  "\x1b[0m"
#define COLOR_GREEN  "\x1b[32m"
#define COLOR_YELLOW "\x1b[33m"
#define COLOR_RED    "\x1b[31m"
#define COLOR_CYAN   "\x1b[36m"

// ==============================================================================
// 전역 상태 및 리소스 변수
// ==============================================================================
atomic_int is_running = 1;              
int serv_sock = -1;                     
int serial_fd = -1;                     
int client_sockets[MAX_CLIENTS] = {0};  

typedef struct DBTask {
    char query[2048];
    struct DBTask* next;
} DBTask;

DBTask* db_queue_head = NULL;
DBTask* db_queue_tail = NULL;
pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER; 
pthread_cond_t db_cond = PTHREAD_COND_INITIALIZER;    

// ==============================================================================
// 유틸리티 함수
// ==============================================================================
long long current_timestamp() {
    struct timeval te; 
    gettimeofday(&te, NULL);
    return te.tv_sec * 1000LL + te.tv_usec / 1000;
}

float apply_low_pass_filter(float raw_val, float prev_filtered_val, float alpha) {
    return (alpha * raw_val) + ((1.0f - alpha) * prev_filtered_val);
}

void reset_gesture_state(int *count, int *fired, int *last_dir) {
    *count = *fired = *last_dir = 0;
}

// ==============================================================================
// 네트워크 통신 함수
// ==============================================================================
int send_all(int sock, const char* buf, int len) {
    int total = 0;
    while (total < len) {
        int n = send(sock, buf + total, len - total, MSG_NOSIGNAL);
        if (n <= 0) return -1;
        total += n;
    }
    return total;
}

void broadcast_to_clients(const char* msg) {
    int len = strlen(msg);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] != 0) {
            if (send_all(client_sockets[i], msg, len) == -1) {
                close(client_sockets[i]);
                client_sockets[i] = 0; 
            }
        }
    }
}

// ==============================================================================
// 데이터베이스 로직 함수
// ==============================================================================
void send_db_stats_to(MYSQL *conn, int clnt_sock) {
    int ppt = 0, yt = 0, fail = 0;
    
    if (mysql_ping(conn) == 0) { 
        if (!mysql_query(conn, "SELECT app_title, status, COUNT(*) FROM gesture_results GROUP BY app_title, status")) {
            MYSQL_RES *result = mysql_store_result(conn);
            if (result) {
                MYSQL_ROW row;
                while ((row = mysql_fetch_row(result))) {
                    if (row[0] && row[1] && row[2]) {
                        int cnt = atoi(row[2]);
                        if (strcmp(row[1], "FAIL") == 0) fail += cnt;
                        else if (strcmp(row[0], "PowerPoint") == 0) ppt += cnt;
                        else if (strcmp(row[0], "YouTube") == 0) yt += cnt;
                    }
                }
                mysql_free_result(result);
            }
        }
    }
    char stat_msg[128];
    sprintf(stat_msg, "STAT|%d|%d|%d\n", ppt, yt, fail);
    send_all(clnt_sock, stat_msg, strlen(stat_msg));
}

void enqueue_query(const char* query) {
    DBTask* new_task = (DBTask*)malloc(sizeof(DBTask));
    strncpy(new_task->query, query, sizeof(new_task->query));
    new_task->next = NULL;

    pthread_mutex_lock(&db_mutex);
    if (db_queue_tail == NULL) {
        db_queue_head = db_queue_tail = new_task;
    } else {
        db_queue_tail->next = new_task;
        db_queue_tail = new_task;
    }
    pthread_cond_signal(&db_cond); 
    pthread_mutex_unlock(&db_mutex);
}

// ==============================================================================
// 스레드(Thread) 함수
// ==============================================================================
void* db_worker_thread(void* arg) {
    MYSQL *worker_conn = mysql_init(NULL);
    if (!mysql_real_connect(worker_conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, 0, NULL, 0)) {
        fprintf(stderr, COLOR_RED "DB Worker 연결 실패: %s" COLOR_RESET "\n", mysql_error(worker_conn));
        return NULL;
    }
    mysql_set_character_set(worker_conn, "utf8mb4");

    while (atomic_load(&is_running)) {
        pthread_mutex_lock(&db_mutex);
        
        while (db_queue_head == NULL && atomic_load(&is_running)) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 1; 
            pthread_cond_timedwait(&db_cond, &db_mutex, &ts);
        }
        
        if (!atomic_load(&is_running)) {
            pthread_mutex_unlock(&db_mutex);
            break;
        }

        DBTask* task = db_queue_head;
        db_queue_head = task->next;
        if (db_queue_head == NULL) db_queue_tail = NULL;
        pthread_mutex_unlock(&db_mutex);

        if (mysql_ping(worker_conn) != 0) {
            fprintf(stderr, COLOR_YELLOW "DB 끊김 감지! 재연결 시도 중..." COLOR_RESET "\n");
        }
        
        if (mysql_query(worker_conn, task->query) != 0) {
            fprintf(stderr, COLOR_RED "DB 저장 에러: %s" COLOR_RESET "\n", mysql_error(worker_conn));
        } else {
            printf(COLOR_GREEN "[비동기 DB 저장 완료]" COLOR_RESET "\n");
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (client_sockets[i] != 0) send_db_stats_to(worker_conn, client_sockets[i]);
            }
        }
        free(task); 
    }
    mysql_close(worker_conn);
    return NULL;
}

void* command_thread_func(void* arg) {
    char cmd[128];
    while (atomic_load(&is_running)) {
        if (fgets(cmd, sizeof(cmd), stdin) != NULL) {
            cmd[strcspn(cmd, "\n")] = 0; 
            if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
                printf("\n" COLOR_YELLOW "안전 종료 시작..." COLOR_RESET "\n");
                atomic_store(&is_running, 0); 
                break; 
            } 
        }
    }
    return NULL;
}

// ==============================================================================
// 메인 진입점 (이벤트 루프)
// ==============================================================================
int main() {
    MYSQL *main_conn = mysql_init(NULL);
    if (!mysql_real_connect(main_conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, 0, NULL, 0)) {
        fprintf(stderr, COLOR_RED "메인 DB 연결 실패: %s" COLOR_RESET "\n", mysql_error(main_conn));
        return 1;
    }
    mysql_set_character_set(main_conn, "utf8mb4");
    printf(COLOR_GREEN "메인 DB 연결 성공 (라이브러리 초기화 완료)" COLOR_RESET "\n");

    pthread_t cmd_thread, db_thread;
    pthread_create(&cmd_thread, NULL, command_thread_func, NULL);
    pthread_create(&db_thread, NULL, db_worker_thread, NULL);

    serial_fd = open(ARDUINO_PORT, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (serial_fd != -1) {
        struct termios options;
        tcgetattr(serial_fd, &options);
        cfsetispeed(&options, B115200); cfsetospeed(&options, B115200);
        options.c_cflag |= (CLOCAL | CREAD);
        options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
        tcsetattr(serial_fd, TCSANOW, &options);
        printf(COLOR_GREEN "아두이노 연결 성공" COLOR_RESET "\n");
    } else {
        printf(COLOR_RED "아두이노 연결 실패 (장치를 확인하세요)" COLOR_RESET "\n");
    }

    serv_sock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PORT);
    bind(serv_sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    listen(serv_sock, MAX_CLIENTS);

    printf(COLOR_CYAN "리눅스 통합 서버 가동 (DB 비동기 + Multi-Client + select 아키텍처)" COLOR_RESET "\n");

    char serial_buf[512];
    int serial_pos = 0;
    float filtered_roll = 0.0f;  
    float alpha = 0.2f;          
    int init_samples_count = 0;
    float init_roll_sum = 0.0f;
    long long last_gesture_time = 0;
    int continuous_count = 0, b_fired = 0, last_dir = 0;
    float trigger_angle = 30.0f; 

    while (atomic_load(&is_running)) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        
        int max_sd = serv_sock;
        FD_SET(serv_sock, &read_fds); 
        
        if (serial_fd != -1) {
            FD_SET(serial_fd, &read_fds); 
            if (serial_fd > max_sd) max_sd = serial_fd;
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockets[i] > 0) FD_SET(client_sockets[i], &read_fds);
            if (client_sockets[i] > max_sd) max_sd = client_sockets[i];
        }

        struct timeval timeout = {1, 0};
        int activity = select(max_sd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity < 0 && errno != EINTR) break;
        if (activity == 0) continue; 

        if (FD_ISSET(serv_sock, &read_fds)) {
            struct sockaddr_in clnt_addr;
            socklen_t clnt_addr_sz = sizeof(clnt_addr);
            int new_socket = accept(serv_sock, (struct sockaddr*)&clnt_addr, &clnt_addr_sz);
            
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (client_sockets[i] == 0) {
                    client_sockets[i] = new_socket;
                    printf(COLOR_GREEN "새 클라이언트 접속 [ID: %d, IP: %s]" COLOR_RESET "\n", i, inet_ntoa(clnt_addr.sin_addr));
                    send_db_stats_to(main_conn, new_socket);
                    break;
                }
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = client_sockets[i];
            if (sd > 0 && FD_ISSET(sd, &read_fds)) {
                char recv_buf[1024];
                int recv_len = recv(sd, recv_buf, sizeof(recv_buf) - 1, 0);
                
                if (recv_len <= 0) {
                    printf(COLOR_YELLOW "클라이언트 연결 해제 [ID: %d]" COLOR_RESET "\n", i);
                    close(sd);
                    client_sockets[i] = 0;
                } else {
                    recv_buf[recv_len] = '\0';
                    char* saveptr;
                    char* type = strtok_r(recv_buf + 4, "|", &saveptr);
                    char* status = strtok_r(NULL, "|", &saveptr);
                    char* title = strtok_r(NULL, "|", &saveptr);
                    char* r_val_str = strtok_r(NULL, "|", &saveptr);
                    char* l_val_str = strtok_r(NULL, "\n", &saveptr); 

                    if (type && status && title) {
                        float roll_val = 0; int lux_val = 0;
                        if (r_val_str) sscanf(r_val_str, "R:%f", &roll_val);
                        if (l_val_str) sscanf(l_val_str, "L:%d", &lux_val);

                        char esc_type[128], esc_status[128], esc_title[256];
                        mysql_real_escape_string(main_conn, esc_type, type, strlen(type));
                        mysql_real_escape_string(main_conn, esc_status, status, strlen(status));
                        mysql_real_escape_string(main_conn, esc_title, title, strlen(title));

                        char query[2048];
                        sprintf(query, "INSERT INTO gesture_results (gesture_type, status, app_title, roll_val, lux_val) VALUES ('%s', '%s', '%s', %.2f, %d)", esc_type, esc_status, esc_title, roll_val, lux_val);
                        
                        enqueue_query(query);
                    }
                }
            }
        }

        if (serial_fd != -1 && FD_ISSET(serial_fd, &read_fds)) {
            char temp_buf[128];
            int bytes_read = read(serial_fd, temp_buf, sizeof(temp_buf) - 1);
            if (bytes_read > 0) {
                for (int i = 0; i < bytes_read; i++) {
                    serial_buf[serial_pos++] = temp_buf[i];
                    if (temp_buf[i] == '\n' || serial_pos >= sizeof(serial_buf) - 1) {
                        serial_buf[serial_pos] = '\0';
                        
                        float pitch, roll; int lux;
                        if (sscanf(serial_buf, "%f,%f,%d", &pitch, &roll, &lux) == 3) {
                            if (roll > 180.0f) roll -= 360.0f;
                            else if (roll < -180.0f) roll += 360.0f;

                            if (init_samples_count < 5) {
                                init_roll_sum += roll;
                                if (++init_samples_count == 5) {
                                    filtered_roll = init_roll_sum / 5.0f;
                                    printf(COLOR_YELLOW "캘리브레이션 완료 (초기값: %.1f)" COLOR_RESET "\n", filtered_roll);
                                }
                            } else {
                                filtered_roll = apply_low_pass_filter(roll, filtered_roll, alpha);

                                char msg[128];
                                sprintf(msg, "P:%.2f R:%.2f L:%d\n", pitch, filtered_roll, lux);
                                broadcast_to_clients(msg); 

                                long long current_time = current_timestamp();
                                if (current_time - last_gesture_time >= 500) { 
                                    if (lux < 300) { 
                                        int current_dir = 0;
                                        if (filtered_roll > trigger_angle) current_dir = 1; 
                                        else if (filtered_roll < -trigger_angle) current_dir = 2; 

                                        if (current_dir != 0) {
                                            if (!b_fired) {
                                                if (current_dir == last_dir) continuous_count++;
                                                else { continuous_count = 1; last_dir = current_dir; }

                                                if (continuous_count >= 3) {
                                                    char cmdMsg[32];
                                                    sprintf(cmdMsg, "CMD|%s\n", current_dir == 1 ? "NEXT" : "PREV");
                                                    broadcast_to_clients(cmdMsg); 
                                                    
                                                    printf(COLOR_CYAN "제스처 발동: %s" COLOR_RESET "\n", current_dir == 1 ? "NEXT" : "PREV");
                                                    last_gesture_time = current_time;
                                                    continuous_count = 0; b_fired = 1; 
                                                }
                                            }
                                        } else if (fabs(filtered_roll) < trigger_angle * 0.5f) {
                                            reset_gesture_state(&continuous_count, &b_fired, &last_dir);
                                        }
                                    } else {
                                        reset_gesture_state(&continuous_count, &b_fired, &last_dir);
                                    }
                                }
                            }
                        }
                        serial_pos = 0; 
                    }
                }
            }
        }
    }

    // 🌟 6. 프로그램 종료 (Graceful Shutdown)
    printf("\n" COLOR_GREEN "리소스 정리 및 큐 소진 대기 중..." COLOR_RESET "\n");
    
    pthread_cond_signal(&db_cond);
    pthread_join(db_thread, NULL); 

    // TCP FIN 패킷 전송을 위한 완벽한 shutdown 적용
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_sockets[i] != 0) {
            shutdown(client_sockets[i], SHUT_RDWR); // 정중하게 차단
            close(client_sockets[i]);
        }
    }
    if (serv_sock != -1) {
        shutdown(serv_sock, SHUT_RDWR); 
        close(serv_sock);
    }
    
    if (serial_fd != -1) close(serial_fd);
    if (main_conn != NULL) mysql_close(main_conn);
    
    printf(COLOR_GREEN "서버가 안전하게 종료되었습니다." COLOR_RESET "\n");
    return 0;
}
