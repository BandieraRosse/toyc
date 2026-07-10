/* EXPECT_ERR: expected identifier */
int main() {
    return 0;
}
/* $ is an unrecognized character, caught by parse_declarator */
int $x;
