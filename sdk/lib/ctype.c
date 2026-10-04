/* C locale ctype helpers; every function takes the value as unsigned char. */
#include <ctype.h>
int isdigit(int character) { return character >= '0' && character <= '9'; }
int isxdigit(int character) {
    return isdigit(character) || (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
}
int isupper(int character) { return character >= 'A' && character <= 'Z'; }
int islower(int character) { return character >= 'a' && character <= 'z'; }
int isalpha(int character) { return isupper(character) || islower(character); }
int isalnum(int character) { return isalpha(character) || isdigit(character); }
int isspace(int character) {
    return character == ' ' || character == '\t' || character == '\n' ||
           character == '\v' || character == '\f' || character == '\r';
}
int isblank(int character) { return character == ' ' || character == '\t'; }
int iscntrl(int character) { return (character >= 0 && character < 0x20) || character == 0x7f; }
int isprint(int character) { return character >= 0x20 && character < 0x7f; }
int isgraph(int character) { return character > 0x20 && character < 0x7f; }
int ispunct(int character) { return isgraph(character) && !isalnum(character); }
int tolower(int character) { return isupper(character) ? character + ('a'-'A') : character; }
int toupper(int character) { return islower(character) ? character - ('a'-'A') : character; }
