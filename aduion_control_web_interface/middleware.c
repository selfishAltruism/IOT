#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <microhttpd.h>
#include <wiringPi.h>
#include <wiringSerial.h>
#include <errno.h>
#include <time.h>

#define PORT 8888
#define SERIAL_PORT "/dev/ttyACM0"
#define BAUD_RATE 9600

int fd;

// 시리얼 포트 초기화 함수
int initialize_serial_connection(const char *port_name, int baud_rate) {
    int fd = serialOpen(port_name, baud_rate);
    if (fd < 0) {
        fprintf(stderr, "Unable to open serial device: %s\n", strerror(errno));
        return -1;
    }
    return fd;
}

// 메시지 송신 및 수신 함수
void send_and_receive(int fd, const char *message) {
    serialPuts(fd, message);
    serialPuts(fd, "\n"); // 메시지의 끝을 알리는 개행 추가
    usleep(100000); // 100ms 대기 후 응답 수신 시도

    char buffer[256];
    int index = 0;

    while (serialDataAvail(fd)) {
        char newChar = serialGetchar(fd);
        if (newChar == '\n' || newChar == '\r') {
            buffer[index] = '\0';
            break;
        } else {
            buffer[index++] = newChar;
        }
    }

    if (index > 0) {
        printf("Received: %s\n", buffer);
    }
}

// 클라이언트로 보낼 응답 생성
struct MHD_Response *generate_response(const char *message) {
    return MHD_create_response_from_buffer(strlen(message), (void *)message, MHD_RESPMEM_MUST_COPY);
}

// CORS 헤더 추가 함수
void add_cors_headers(struct MHD_Response *response) {
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
    MHD_add_response_header(response, "Access-Control-Allow-Methods", "GET, OPTIONS");
    MHD_add_response_header(response, "Access-Control-Allow-Headers", "Content-Type");
}

// 연결에 대한 응답 처리
static enum MHD_Result answer_to_connection(void *cls, struct MHD_Connection *connection,
                                            const char *url, const char *method,
                                            const char *version, const char *upload_data,
                                            size_t *upload_data_size, void **con_cls) {
    if (strcmp(method, "OPTIONS") == 0) {
        struct MHD_Response *response = generate_response("");
        add_cors_headers(response);
        return MHD_queue_response(connection, MHD_HTTP_OK, response);
    }

    if (strcmp(method, "GET") == 0) {
        fd = initialize_serial_connection(SERIAL_PORT, BAUD_RATE);
        if (fd == -1) {
            return MHD_NO;
        }

        const char *response_message = "Unknown request.";
        if (strcmp(url, "/runCamera") == 0) {
            printf("run camera...\n");
            system("sudo v4l2-ctl --device=/dev/video0 --set-fmt-video=width=1920,height=1080,pixelformat=MJPG --stream-mmap --stream-count=1 --stream-to=capture.jpg");

            send_and_receive(fd, "C");
            response_message = "runCamera executed.";
        } else if (strncmp(url, "/runSegment/", 12) == 0) {
            const char *contentMessage = url + 12; // 메시지 시작 위치
            printf("Received message for runSegment: %s\n", contentMessage);

            char message[50] = "S";
            strcat(message, contentMessage);

            send_and_receive(fd, message);
            response_message = "runSegment executed.";
        } else if (strncmp(url, "/runLCD/", 8) == 0) {
            const char *contentMessage = url + 8; // 메시지 시작 위치
            printf("Received message for runSegment: %s\n", contentMessage);

            char message[50] = "L";
            strcat(message, contentMessage);

            send_and_receive(fd, message);
            response_message = "runLCD executed.";
        }

        close(fd); // 시리얼 포트 닫기
        struct MHD_Response *response = generate_response(response_message);
        add_cors_headers(response); // CORS 헤더 추가
        return MHD_queue_response(connection, MHD_HTTP_OK, response);
    }

    return MHD_NO; 
}

int main() {
    int fd = initialize_serial_connection(SERIAL_PORT, BAUD_RATE);
    send_and_receive(fd, "CAMERA");

    struct MHD_Daemon *daemon;
    daemon = MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD, PORT, NULL, NULL,
                               &answer_to_connection, NULL, MHD_OPTION_END);

    if (daemon == NULL) {
        fprintf(stderr, "Failed to start the HTTP daemon.\n");
        return 1;
    }

    printf("Server is running on port %d\n", PORT);

    getchar(); // 서버가 종료되지 않도록 대기
    MHD_stop_daemon(daemon);
    return 0;
}
