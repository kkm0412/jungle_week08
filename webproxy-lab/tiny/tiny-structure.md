# Tiny Server Structure

> [!info]
> `tiny` 서버는 "연결 1개를 받아서 HTTP 요청 1개를 처리하고 닫는" 아주 단순한 반복형(iterative) 웹 서버다.

## 한눈에 보기

```text
Browser <-> tiny server -> files
                         -> cgi-bin
```

- 브라우저가 요청을 보낸다.
- `tiny`가 요청을 읽는다.
- 정적 파일이면 파일 내용을 그대로 응답한다.
- 동적 요청이면 CGI 프로그램을 실행한 결과를 응답한다.

## 핵심 흐름

> [!summary]
> 연결 기다림 -> 하나 받음 -> 하나 처리 -> 닫음 -> 다시 기다림

`main()`은 서버를 열고, 연결을 하나씩 처리한다.

```text
main()
  -> Open_listenfd(port)
  -> while(1)
       -> Accept()
       -> doit(connfd)
       -> Close(connfd)
```

## `/` 요청이 들어왔을 때

브라우저가 대략 이런 요청을 보낸다고 생각하면 된다.

```http
GET / HTTP/1.1
Host: localhost:8000
```

서버 내부에서는 이렇게 흐른다.

```text
Browser
  -> "GET / HTTP/1.1"
  -> doit(fd)
  -> read request line
  -> read headers
  -> parse_uri("/")
  -> filename = "./home.html"
  -> stat("./home.html")
  -> serve_static(...)
  -> Browser gets home.html
```

> [!tip]
> `/` 요청은 결국 `./home.html`로 바뀌어서 정적 파일 응답으로 처리된다.

## 정적 콘텐츠 흐름

예: `/`, `/home.html`, `/logo.png`

```text
Browser
  -> GET /home.html
  -> doit
  -> parse_uri -> static
  -> serve_static
  -> get_filetype
  -> Open / Mmap
  -> Rio_writen(header)
  -> Rio_writen(body)
  -> Browser renders file
```

데이터 흐름만 따로 보면:

```text
home.html -> tiny server -> HTTP response -> browser
```

## 동적 콘텐츠 흐름

예: `/cgi-bin/adder?1&2`

```text
Browser
  -> GET /cgi-bin/adder?1&2
  -> doit
  -> parse_uri -> dynamic
  -> filename = "./cgi-bin/adder"
  -> cgiargs = "1&2"
  -> serve_dynamic
  -> Fork()
     -> child: setenv -> Dup2 -> Execve
     -> parent: Wait()
  -> Browser gets CGI output
```

핵심은:

```text
CGI stdout -> socket(fd) -> browser
```

> [!note]
> CGI 프로그램이 `printf`로 출력한 내용이 그대로 브라우저 응답으로 간다.

## 함수 호출 관계

```text
main
  -> Open_listenfd
  -> Accept
  -> doit
     -> Rio_readinitb
     -> Rio_readlineb
     -> read_requesthdrs
     -> parse_uri
     -> stat
     -> serve_static
        -> get_filetype
        -> Open
        -> Mmap
        -> Rio_writen
     -> serve_dynamic
        -> Fork
        -> setenv
        -> Dup2
        -> Execve
        -> Wait
  -> Close
```

## 코드와 연결해서 보기

- `main`: 서버 소켓 생성, 연결 수락, `doit()` 호출
- `doit`: 요청 라인 파싱, 헤더 읽기, 정적/동적 분기
- `read_requesthdrs`: 헤더를 끝까지 읽고 지금은 무시
- `parse_uri`: URI를 파일 경로 또는 CGI 경로로 변환
- `serve_static`: 파일을 읽어 HTTP 응답으로 전송
- `serve_dynamic`: CGI 프로그램을 실행하고 출력을 브라우저로 전달

## 한 줄 요약

```text
Browser request
  -> Accept
  -> doit
  -> static ? file response
  -> dynamic ? CGI response
  -> Close
```
