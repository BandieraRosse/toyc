#ifndef TOYC_RASTERFALL_HUMANOID_BASIS_H
#define TOYC_RASTERFALL_HUMANOID_BASIS_H

#include "rasterfall_humanoid.h"

struct rasterfall_humanoid_point {
    double value[3];
    int valid;
};

struct rasterfall_humanoid_basis_input {
    struct rasterfall_humanoid_point bones[RASTERFALL_HUMANOID_BONE_COUNT];
    struct rasterfall_humanoid_point left_toe, right_toe;
    struct rasterfall_humanoid_point left_middle, right_middle;
    struct rasterfall_humanoid_point left_thumb, right_thumb;
    double model_up[3];
    double model_forward[3];
};

enum rasterfall_humanoid_basis_confidence {
    RASTERFALL_HUMANOID_BASIS_LOW,
    RASTERFALL_HUMANOID_BASIS_MEDIUM,
    RASTERFALL_HUMANOID_BASIS_HIGH
};

struct rasterfall_humanoid_rest_basis {
    double primary[3];
    double secondary[3];
    double third[3];
    double rotation[4]; /* x, y, z, w; axes above are matrix columns. */
    const char *source;
    int confidence;
    int valid;
};

int rasterfall_humanoid_build_rest_bases(
    const struct rasterfall_humanoid_basis_input *input,
    struct rasterfall_humanoid_rest_basis *bases);
int rasterfall_humanoid_validate_rest_bases(
    const struct rasterfall_humanoid_rest_basis *bases,
    double *maximum_error);
int rasterfall_humanoid_validate_anatomy(
    const struct rasterfall_humanoid_rest_basis *bases);
int rasterfall_humanoid_basis_logic_test(void);
const char *rasterfall_humanoid_basis_confidence_name(int confidence);

#endif
