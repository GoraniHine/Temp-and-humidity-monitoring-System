#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pigpio.h>
#include <arpa/inet.h>

#define PORT 12345
#define DHT_PIN 4

int readDHT22(int gpio, float* temperature, float* humidity) {
    uint8_t data[5] = {0};
    int last_state = PI_HIGH;
    int counter = 0;
    int j = 0;

    gpioSetMode(gpio, PI_OUTPUT);
    gpioWrite(gpio, PI_LOW);
    gpioDelay(18000); // 18ms
    gpioWrite(gpio, PI_HIGH);
    gpioDelay(30);    // 30us
    gpioSetMode(gpio, PI_INPUT);

    // DHT22는 총 85개의 신호를 보냄 (이 중 40비트 유효 4~43비트)
    for (int i = 0; i < 85; i++) {
        counter = 0;
        while (gpioRead(gpio) == last_state) {
            counter++;
            gpioDelay(1);
            if (counter == 255) break;
        }
        last_state = gpioRead(gpio);

        if (counter == 255) break;

        // 데이터는 4번째 신호부터 시작
        if ((i >= 4) && (i % 2 == 0)) {
            data[j / 8] <<= 1;
            if (counter > 16) // 긴 펄스는 1, 짧은 펄스는 0
                data[j / 8] |= 1; // 비트 or 연산
            j++;
        }
    }

    // 체크섬 확인
    if ((j >= 40) &&
        (data[4] == ((data[0] + data[1] + data[2] + data[3]) & 0xFF))) {
        
        *humidity = ((data[0] << 8) + data[1]) * 0.1;
        *temperature = (((data[2] & 0x7F) << 8) + data[3]) * 0.1;
        if (data[2] & 0x80) *temperature *= -1;
        return 0; // 성공
    } else {
        return 1; // 실패
    }
}

int main() 
{
    // GPIO 초기화
    if (gpioInitialise() < 0) {
        printf("pigpio 초기화 실패\n");
        return 1;  // 실패시 프로그램 종료
    }

    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    char buffer[1024] = {0};
    socklen_t addr_len = sizeof(client_addr);

    float temperature = 0.0, humidity = 0.0;

    // 1. 소켓 생성
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return 1;
    }

    // 2. 서버 주소 정보 설정
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // 3. 바인드
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        return 1;
    }

    // 4. 대기
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        return 1;
    }

    printf("서버 대기 중... (포트 %d)\n", PORT);

    // 5. 클라이언트 연결 수락
    client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
    if (client_fd < 0) 
    {
        perror("accept");
        return 1;
    }

    printf("클라이언트 연결됨\n");

    // 6. 데이터 수신 및 응답
    while (1) 
    {
        memset(buffer, 0, sizeof(buffer));
    
        int bytes_read = read(client_fd, buffer, sizeof(buffer));
        
        if (bytes_read <= 0) 
        {
            printf("클라이언트 연결 종료됨\n");
            break;
        }
        
        int sensor_check = readDHT22(DHT_PIN, &temperature, &humidity);
        
        if (temperature > 35) // 센서로부터 값을 받아서 온도가 35도 이상 혹은 습도가 60도 이상이 되면 클라이언트에게 위험 경고 메시지를 전송
        {   
            send(client_fd, "비정상적인 온도가 감지되었습니다.", strlen("비정상적인 온도가 감지되었습니다."), 0);
        }
        else if(humidity > 60)
        {
            send(client_fd, "비정상적인 습도가 감지되었습니다.", strlen("비정상적인 습도가 감지되었습니다."), 0);
        }
        
        printf("클라이언트로부터 수신: %s\n", buffer);

        if (strncmp(buffer, "GET", 3) == 0) // 클라이언트로부터 'GET' 명령을 수신받으면, 온도와 습도를 클라이언트로 전송
        {
            if (sensor_check == 0) 
            {
                snprintf(buffer, sizeof(buffer), "온도: %.1f°C, 습도: %.1f%%", temperature, humidity);
            } 
            else 
            {
                snprintf(buffer, sizeof(buffer), "센서 읽기 실패");
            }
            send(client_fd, buffer, strlen(buffer), 0);
        } 
        else if (strncmp(buffer, "EXIT", 4) == 0) 
        {
            printf("클라이언트 종료 요청\n");
            break;
        } 
        else 
        {
            send(client_fd, "알 수 없는 명령입니다", strlen("알 수 없는 명령입니다"), 0);
        }   
    }

    // 7. 종료
    close(client_fd);
    close(server_fd);
    gpioTerminate();  // 종료 시 GPIO 자원 해제
    return 0;
}
