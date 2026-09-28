#include "core/indicators/linreg.h"

#include <math.h>
#include <string.h>

bool tr_ols_fit(const double *y, size_t n, double x0, size_t min_samples, tr_ols_result_t *out) {
    if (y == 0 || out == 0 || n == 0) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (n < min_samples) {
        return true; /* 표본 부족: valid=false로 보고 */
    }
    double sum_x = 0.0, sum_y = 0.0, sum_xy = 0.0, sum_x2 = 0.0, sum_y2 = 0.0;
    for (size_t i = 0; i < n; i++) {
        double x = x0 + (double)i;
        sum_x += x;
        sum_y += y[i];
        sum_xy += x * y[i];
        sum_x2 += x * x;
        sum_y2 += y[i] * y[i];
    }
    double dn = (double)n;
    double denom = dn * sum_x2 - sum_x * sum_x; /* 연속 정수 x라 항상 > 0 */
    double slope = 0.0;
    if (denom > 0.0) {
        slope = (dn * sum_xy - sum_x * sum_y) / denom;
    }
    double intercept = (sum_y - slope * sum_x) / dn;

    double r2 = 0.0;
    double r2_num = dn * sum_xy - sum_x * sum_y;
    double r2_den_y = dn * sum_y2 - sum_y * sum_y;
    if (denom > 0.0 && r2_den_y > 0.0) {
        r2 = (r2_num * r2_num) / (denom * r2_den_y);
        if (r2 < 0.0) {
            r2 = 0.0;
        }
        if (r2 > 1.0) {
            r2 = 1.0;
        }
    }
    /* 원본 관례: SSE = SST*(1-R²) */
    double sst = sum_y2 - sum_y * sum_y / dn;
    double sse = sst * (1.0 - r2);
    double residual_sd = 0.0;
    if (n > 2 && sse > 0.0) {
        residual_sd = sqrt(sse / (double)(n - 2));
    }

    out->slope = slope;
    out->intercept = intercept;
    out->r2 = r2;
    out->residual_sd = residual_sd;
    out->current = slope * (x0 + (double)(n - 1)) + intercept;
    out->valid = true;
    return true;
}
