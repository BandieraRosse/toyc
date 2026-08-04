/* Pending regression: a three-float struct returned through the hidden pointer. */
struct vec3 { float x, y, z; };

static struct vec3 transform(struct vec3 v)
{
    struct vec3 r;
    r.x = v.x + 1.0f;
    r.y = v.y * 2.0f;
    r.z = v.z - 3.0f;
    return r;
}

int main(void)
{
    struct vec3 v;
    struct vec3 r;
    v.x = 2.0f; v.y = 4.0f; v.z = 8.0f;
    r = transform(v);
    if (r.x != 3.0f) return 1;
    if (r.y != 8.0f) return 2;
    if (r.z != 5.0f) return 3;
    return 0;
}
