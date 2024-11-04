#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <microhttpd.h>
#include <wiringPi.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

#define PORT 8888

int initialize_serial_connection(const char *port_name, int baud_rate) {
    int fd = open(port_name, O_RDWR | O_NOCTTY );
    if (fd == -1) {
        perror("open_port: Unable to open serial port");
        return -1;
    }

    struct termios options;
    tcgetattr(fd, &options);
    cfsetispeed(&options, baud_rate);
    cfsetospeed(&options, baud_rate);
    options.c_cflag |= (CLOCAL | CREAD);  // 로컬 연결, 수신 활성화
    options.c_cflag &= ~PARENB;           // 패리티 비트 사용 안 함
    options.c_cflag &= ~CSTOPB;           // 스톱 비트 1
    options.c_cflag &= ~CSIZE;            // 데이터 비트 설정 초기화
    options.c_cflag |= CS8;               // 데이터 비트 8
    tcsetattr(fd, TCSANOW, &options);

    return fd;
}

void send_and_receive(int fd, const char *message) {
    char buffer[256];
    int n;

    // 메시지 전송
    ssize_t bytes_written = write(fd, message, strlen(message));
    if (bytes_written < 0) {
        perror("Error writing to serial port");
        return;
    } else if (bytes_written < strlen(message)) {
        printf("Warning: not all data written to serial port.\n");
    }

    // 수신
    usleep(100000); // 100ms 대기 후 응답 수신 시도
    n = read(fd, buffer, sizeof(buffer) - 1);
    if (n < 0) {
        perror("Error reading from serial port");
        return;
    } else if (n == 0) {
        printf("No data received.\n");
    } else {
        buffer[n] = '\0'; // null 종료
        printf("Received: %s\n", buffer);
    }
}

// 클라이언트로 보낼 응답 생성
struct MHD_Response *generate_response(const char *message) {
    return MHD_create_response_from_buffer(strlen(message), (void *)message, MHD_RESPMEM_MUST_COPY);
}

// 연결에 대한 응답 처리
static enum MHD_Result answer_to_connection(void *cls, struct MHD_Connection *connection,
                                            const char *url, const char *method,
                                            const char *version, const char *upload_data,
                                            size_t *upload_data_size, void **con_cls) {
        const char *port = "/dev/ttyACM0";

        if (strcmp(method, "GET") == 0) {
            if (strcmp(url, "/runCamera") == 0) {
                printf("run camera..");
                system("sudo v4l2-ctl --device=/dev/video0 --set-fmt-video=width=1920,height=1080,pixelformat=MJPG --stream-mmap --stream-count=1 --stream-to=capture.jpg");

                // 아두이노 시리얼 통신 //
                int fd = initialize_serial_connection(port , B9600);
                if (fd == -1) {
                    return 1;
                }
                send_and_receive(fd, "CAMERA");
                close(fd);

                // Res
                const char *response_message = "runCamera executed.";
                struct MHD_Response *response = generate_response(response_message);
                return MHD_queue_response(connection, MHD_HTTP_OK, response);
            }
            
            // /runSegment/(message) 처리
            if (strncmp(url, "/runSegment/", 12) == 0) {
                const char *contentMessage = url + 12; // 메시지 시작 위치
                printf("Received message for runSegment: %s\n", contentMessage); // 메시지 출력

                // 아두이노 시리얼 통신 //
                int fd = initialize_serial_connection(port, B9600);
                if (fd == -1) {
                    printf("Error in connect serial");
                    return 1;
                }
                printf("Connect serial");
                
                send_and_receive(fd, "SEGMENT");
                send_and_receive(fd, contentMessage);
                close(fd);

                // Res
                const char *response_message = "runSegment executed.";
                struct MHD_Response *response = generate_response(response_message);
                return MHD_queue_response(connection, MHD_HTTP_OK, response);
            }

            // /runLCD/(message) 처리
            if (strncmp(url, "/runLCD/", 5) == 0) {
                const char *contentMessage = url + 8; // 메시지 시작 위치
                printf("Received message for LCD: %s\n", contentMessage); // 메시지 출력

                // 아두이노 시리얼 통신 //
                int fd = initialize_serial_connection(port, B9600);
                if (fd == -1) {
                    return 1;
                }
                send_and_receive(fd, "LCD");
                send_and_receive(fd, contentMessage);
                close(fd);

                // Res
                const char *response_message = "runSegment executed.";
                struct MHD_Response *response = generate_response(response_message);
                return MHD_queue_response(connection, MHD_HTTP_OK, response);   
            }

            // 알 수 없는 경로 처리
            const char *response_message = "Unknown request.";
            struct MHD_Response *response = generate_response(response_message);
            return MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, response);
        }
        
        // 메서드가 GET이 아닐 경우
        return MHD_NO; 
    }

int main() {
    // 웹 Local http 통신 // 
    struct MHD_Daemon *daemon;

    daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, PORT, NULL, NULL,
                               &answer_to_connection, NULL, MHD_OPTION_END);

    if (daemon == NULL) {
        fprintf(stderr, "Failed to start the HTTP daemon.\n");
        return 1;
    }

    printf("Server is running on port %d\n", PORT);

    getchar(); // 서버가 종료되지 않도록 대기

    // Close //
    MHD_stop_daemon(daemon);
    return 0;
}