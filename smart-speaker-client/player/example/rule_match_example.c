#include <stdio.h>
#include <string.h>
#include "rule_match.h"

int main(void)
{
    char input[512];
    rule_match_result_t result;

    printf("请输入指令文本，输入 exit 退出\n");
    while (1) {
        printf("> ");
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }
        input[strcspn(input, "\n")] = '\0';
        if (strcmp(input, "exit") == 0) {
            break;
        }
        if (rule_match_text(input, &result) != 0) {
            printf("match_error\n");
            continue;
        }
        if (result.matched) {
            if (result.cmd == RULE_CMD_VOL_SET && result.vol_set_target >= 0) {
                printf("matched=1 cmd=%s action=%s vol_set=%d\n",
                       rule_cmd_to_string(result.cmd),
                       result.action_desc,
                       result.vol_set_target);
            } else {
                printf("matched=1 cmd=%s action=%s\n",
                       rule_cmd_to_string(result.cmd),
                       result.action_desc);
            }
        } else {
            printf("matched=0 cmd=%s action=%s\n",
                   rule_cmd_to_string(result.cmd),
                   result.action_desc);
        }
    }
    return 0;
}
