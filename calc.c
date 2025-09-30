#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]){

        int num1 = atof(argv[1]);
        char op = argv[2][0];
        int num2 = atoi(argv[3]);
        int result = 0;

        switch (op) {
                case '+':
                        result = num1 + num2;
                        break;
                case '-':
                        result = num1 - num2;
                        break;
                case '*':
                case 'x':
                case 'X':
                        result = num1 * num2;
                        break;
                case '/':
                        if(num2==0){
                                printf("Error: Cannot be divided by 0.");
                                return 1;
                        }
                        result = num1 / num2;
                        break;
                default:
                        printf("It's an operator that dosen't support it : %c\n", op);
                        return 1;
        }

        printf("%d %c %d = %d\n", num1, op, num2, result);

        return 0;
}
