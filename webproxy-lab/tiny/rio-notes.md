# RIO 정리

> [!info]
> `rio_t`는 CS:APP에서 제공하는 Robust I/O용 구조체다.  
> 파일 디스크립터(fd)에서 데이터를 안전하게 읽기 위해 내부 버퍼를 가지고 있다.

## fd란?

Unix/Linux에서는 파일, 소켓, 파이프, 터미널 같은 I/O 대상을 대부분 fd 번호로 다룬다.

```text
0 -> 표준 입력(stdin)
1 -> 표준 출력(stdout)
2 -> 표준 에러(stderr)
3 -> 열린 파일 또는 소켓
4 -> 또 다른 파일 또는 소켓
...
```

`tiny.c`에서 `doit(fd)`의 `fd`는 보통 `Accept()`가 반환한 클라이언트 연결 소켓이다.

```c
connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
doit(connfd);
```

즉 `doit()` 안의 `fd`는 브라우저와 연결된 소켓을 가리킨다.

## rio_t 안에는 뭐가 있나?

`rio_t`는 대략 이런 구조다.

```c
typedef struct {
    int rio_fd;
    int rio_cnt;
    char *rio_bufptr;
    char rio_buf[RIO_BUFSIZE];
} rio_t;
```

각 필드의 의미:

- `rio_fd`: 어떤 fd에서 읽을지 저장
- `rio_cnt`: 내부 버퍼에 아직 안 읽은 바이트 수
- `rio_bufptr`: 내부 버퍼에서 다음에 읽을 위치
- `rio_buf`: fd에서 읽어온 데이터를 잠시 저장하는 내부 버퍼

## Rio_readinitb

```c
Rio_readinitb(&rio, fd);
```

의미:

```text
rio 구조체를 초기화한다.
앞으로 rio는 fd가 가리키는 대상에서 읽는다.
```

중요한 점:

```text
Rio_readinitb는 fd에서 데이터를 읽어오지 않는다.
그냥 rio와 fd를 연결하고 내부 상태를 초기화한다.
```

초기화 직후 상태는 대략 이렇다.

```text
rio.rio_fd = fd
rio.rio_cnt = 0
rio.rio_bufptr = rio.rio_buf 시작
rio.rio_buf = 비어 있음
```

## Rio_readlineb

```c
Rio_readlineb(&rio, buf, MAXLINE);
```

의미:

```text
rio가 연결된 fd에서 한 줄을 읽어서 buf에 저장한다.
최대 MAXLINE 바이트까지만 읽는다.
```

`Rio_readlineb`는 보통 다음 중 하나가 될 때까지 읽는다.

- 개행 문자 `\n`을 만날 때까지
- `MAXLINE - 1` 바이트를 읽을 때까지
- EOF를 만날 때까지

## 실제 읽기 흐름

핵심 구조는 이렇게 보면 된다.

```text
fd -> rio 내부 버퍼 -> 사용자 buf
```

더 자세히:

```text
Rio_readinitb
  -> rio와 fd를 연결
  -> 아직 fd에서 읽지는 않음

Rio_readlineb
  -> rio 내부 버퍼에 남은 데이터가 있으면 거기서 읽음
  -> 내부 버퍼가 비어 있으면 fd에서 read()로 데이터를 가져옴
  -> 한 줄을 사용자 buf에 복사
```

## 예시

브라우저가 이런 요청을 보냈다고 하자.

```http
GET / HTTP/1.1
Host: localhost:8000
User-Agent: Chrome

```

실제로는 줄 끝에 `\r\n`이 붙어서 들어온다.

```text
GET / HTTP/1.1\r\n
Host: localhost:8000\r\n
User-Agent: Chrome\r\n
\r\n
```

처음 호출:

```c
Rio_readlineb(&rio, buf, MAXLINE);
```

결과:

```text
buf = "GET / HTTP/1.1\r\n"
rio_bufptr -> "Host: localhost:8000\r\n" 시작 위치
```

다음 호출:

```c
Rio_readlineb(&rio, buf, MAXLINE);
```

결과:

```text
buf = "Host: localhost:8000\r\n"
rio_bufptr -> "User-Agent: Chrome\r\n" 시작 위치
```

또 다음 호출:

```text
buf = "User-Agent: Chrome\r\n"
rio_bufptr -> "\r\n" 시작 위치
```

마지막 호출:

```text
buf = "\r\n"
```

HTTP 요청 헤더는 빈 줄 `"\r\n"`을 만나면 끝난다.

## read_requesthdrs 예시

현재 `tiny.c`의 함수:

```c
void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  while(strcmp(buf, "\r\n")){
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}
```

읽는 순서:

```text
1. 첫 번째 헤더 줄을 읽음
2. 그 줄이 빈 줄인지 확인
3. 빈 줄이 아니면 다음 줄을 읽음
4. 읽은 줄을 출력
5. 빈 줄 "\r\n"을 만날 때까지 반복
```

주의할 점:

```text
이 구현은 첫 번째 헤더 줄을 읽고 바로 출력하지 않는다.
그래서 Host 헤더 같은 첫 번째 헤더는 출력되지 않을 수 있다.
```

더 자연스러운 형태:

```c
void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  while (strcmp(buf, "\r\n")) {
    printf("%s", buf);
    Rio_readlineb(rp, buf, MAXLINE);
  }
}
```

## 한 줄 요약

```text
fd는 읽기 대상의 번호표다.
Rio_readinitb는 rio와 fd를 연결한다.
Rio_readlineb는 필요할 때 fd에서 rio 내부 버퍼로 읽어오고,
그 내부 버퍼에서 한 줄씩 사용자 buf로 복사한다.
```
