/*
 * Pending regression: sizeof(*pointer_to_struct) must equal the struct size.
 * The Wayland client uses an explicit sizeof(struct type) until this is fixed.
 */
struct large_value {
    int words[64];
};

int main(void)
{
    struct large_value *p = (struct large_value *)0;
    return sizeof(*p) == sizeof(struct large_value) ? 0 : 1;
}
