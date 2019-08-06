#include <termios.h>
#include <unistd.h>

void EnableRawModel()
{
    struct termios raw;

    tcgetattr(STDIN_FILENO, &raw);
    raw.c_lflag &= ~(ECHO);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main()
{
    EnableRawModel();
    char c;
    while (read(STDIN_FILENO, &c, 1) && c != 'q');
    return 0;
}
