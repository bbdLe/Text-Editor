#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>

#define CTRL_KEY(k) ((k) & 0x1f)

struct EditorConfig
{
    int screenrows;
    int screencols;
    struct termios orig_termios;
};

struct EditorConfig E;

struct ABuf
{
    char* b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void AbAppend(struct ABuf* ab, const char* s, int len)
{
    char* newBuf = (char*)(realloc(ab->b, ab->len + len));

    if (newBuf == NULL)
    {
        return;
    }
    memcpy(&newBuf[ab->len], s, len);
    ab->b = newBuf;
    ab->len += len;
}

void AbFree(struct ABuf* ab)
{
    free(ab->b);
}

void Die(const char* s)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void DisableRawModel()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    {
        Die("tcsetattr");
    }
}

void EnableRawModel()
{
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    {
        Die("tcsgetattr");
    }
    atexit(DisableRawModel);
    struct termios raw = E.orig_termios;
    raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON | ICRNL);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag &= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    {
        Die("tcsetattr");
    }
}

char EditorReadKey()
{
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if (nread == -1 && errno != EAGAIN)
            Die("read");
    }

    return c;
}

void EditorProcessKey()
{
    char c = EditorReadKey();

    switch (c)
    {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
    }
}

void EditorDrawRows(struct ABuf* aBuf)
{
    int y;

    for (y = 0; y < E.screenrows; ++y)
    {
        AbAppend(aBuf, "~", 1);

        if (y < E.screenrows - 1)
        {
            AbAppend(aBuf, "\r\n", 2);
        }
    }
}

void EditorRefreshScreen()
{
    struct ABuf aBuf = ABUF_INIT;

    AbAppend(&aBuf, "\x1b[2J", 4);
    AbAppend(&aBuf, "\x1b[H", 3);
    EditorDrawRows(&aBuf);
    AbAppend(&aBuf, "\x1b[H", 3);
    write(STDOUT_FILENO, aBuf.b, aBuf.len);
    AbFree(&aBuf);
}

int GetCursorPosition(int* rows, int* cols)
{
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    {
        return -1;
    }

    while (i < sizeof(buf) - 1)
    {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
        {
            break;
        }

        if (buf[i] == 'R')
        {
           break; 
        }

        ++i;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[')
    {
        return -1;
    }
    
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
    {
        return -1;
    }

    return 0;
}

int GetWindowSize(int* rows, int* cols)
{
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
        {
            return -1;
        }
        return GetCursorPosition(rows, cols);
    }
    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

void InitEditor()
{
    if (GetWindowSize(&E.screenrows, &E.screencols) == -1)
    {
        Die("get windows size");
    }
}

int main()
{
    EnableRawModel();
    InitEditor();

    while (1)
    {
        EditorRefreshScreen();
        EditorProcessKey();
    }
    return 0;
}
