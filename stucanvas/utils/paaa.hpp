#pragma once

#include <iostream>
#include <vector>
#include <cmath>
#include <limits>
#include <algorithm>
#include <iomanip>
#include <Eigen/Dense>
#include "function.hpp" // 引入闭包包装器

namespace StuCanvas::utils
{
    /**
     * @brief 归一化自适应多元有理函数模型
     */
    struct BivariateBarycentricRational
    {
        // 归一化空间下的有理函数参数
        Eigen::VectorXd lambda;
        Eigen::VectorXd mu;
        Eigen::MatrixXd alpha;
        Eigen::MatrixXd beta;

        // 全自动尺度缩放与归一化参数
        double x_c = 0.0;
        double dx = 1.0;
        double y_c = 0.0;
        double dy = 1.0;
        double f_c = 0.0;
        double df = 1.0;

        /**
         * @brief 评估任意点处的函数值（支持 NaN/Inf 防御与自动尺度映射）
         */
        double evaluate(double x, double y) const
        {
            if (std::isnan(x) || std::isinf(x) || std::isnan(y) || std::isinf(y)) {
                return std::numeric_limits<double>::quiet_NaN();
            }

            double nx = (x - x_c) / dx;
            double ny = (y - y_c) / dy;

            const int n = static_cast<int>(lambda.size());
            const int m = static_cast<int>(mu.size());
            if (n == 0 || m == 0) return f_c;

            Eigen::VectorXd g_lam(n);
            int x_match_idx = -1;
            for (int i = 0; i < n; ++i) {
                if (std::abs(nx - lambda[i]) < 1e-14) {
                    x_match_idx = i;
                    break;
                }
            }
            if (x_match_idx != -1) {
                g_lam.setZero();
                g_lam[x_match_idx] = 1.0;
            } else {
                for (int i = 0; i < n; ++i) {
                    g_lam[i] = 1.0 / (nx - lambda[i]);
                }
            }

            Eigen::VectorXd g_mu_vec(m);
            int y_match_idx = -1;
            for (int j = 0; j < m; ++j) {
                if (std::abs(ny - mu[j]) < 1e-14) {
                    y_match_idx = j;
                    break;
                }
            }
            if (y_match_idx != -1) {
                g_mu_vec.setZero();
                g_mu_vec[y_match_idx] = 1.0;
            } else {
                for (int j = 0; j < m; ++j) {
                    g_mu_vec[j] = 1.0 / (ny - mu[j]);
                }
            }

            double num = 0.0;
            double den = 0.0;
            for (int i = 0; i < n; ++i) {
                for (int j = 0; j < m; ++j) {
                    double term_basis = g_lam[i] * g_mu_vec[j];
                    num += beta(i, j) * term_basis;
                    den += alpha(i, j) * term_basis;
                }
            }

            if (std::abs(den) < 1e-22) {
                return f_c;
            }

            double norm_z = num / den;
            if (std::isnan(norm_z) || std::isinf(norm_z)) {
                return f_c;
            }

            return norm_z * df + f_c;
        }

        double operator()(double x, double y) const
        {
            return evaluate(x, y);
        }
    };

    /**
     * @brief 零异常版散点数据 p-AAA 拟合器 (由 BivariateBarycentricRational 独立驱动)
     */
    class ScatteredPaaa
    {
    public:
        static BivariateBarycentricRational fit(
            const FfiFunction<double(double, double)>& f,
            const Eigen::VectorXd& X,
            const Eigen::VectorXd& Y,
            double tol = 1e-9,
            int max_iters = 50)
        {
            const int K = static_cast<int>(X.size());

            std::vector<double> valid_X, valid_Y, valid_F;
            valid_X.reserve(K); valid_Y.reserve(K); valid_F.reserve(K);

            for (int p = 0; p < K; ++p) {
                double xp = X[p]; double yp = Y[p];
                if (std::isnan(xp) || std::isinf(xp) || std::isnan(yp) || std::isinf(yp)) continue;
                double fp = f(xp, yp);
                if (std::isnan(fp) || std::isinf(fp)) continue;

                valid_X.push_back(xp);
                valid_Y.push_back(yp);
                valid_F.push_back(fp);
            }

            int valid_count = static_cast<int>(valid_X.size());
            if (valid_count == 0) {
                BivariateBarycentricRational empty_r;
                empty_r.lambda.resize(1); empty_r.lambda[0] = 0.0;
                empty_r.mu.resize(1);     empty_r.mu[0] = 0.0;
                empty_r.alpha.resize(1, 1); empty_r.alpha(0, 0) = 1.0;
                empty_r.beta.resize(1, 1);  empty_r.beta(0, 0) = 0.0;
                return empty_r;
            }

            Eigen::VectorXd sX(valid_count);
            Eigen::VectorXd sY(valid_count);
            Eigen::VectorXd sF(valid_count);
            for (int i = 0; i < valid_count; ++i) {
                sX[i] = valid_X[i];
                sY[i] = valid_Y[i];
                sF[i] = valid_F[i];
            }

            double x_min = sX.minCoeff(); double x_max = sX.maxCoeff();
            double x_c = (x_min + x_max) / 2.0;
            double dx  = std::max((x_max - x_min) / 2.0, 1e-15);

            double y_min = sY.minCoeff(); double y_max = sY.maxCoeff();
            double y_c = (y_min + y_max) / 2.0;
            double dy  = std::max((y_max - y_min) / 2.0, 1e-15);

            double f_min = sF.minCoeff(); double f_max = sF.maxCoeff();
            double f_c = (f_min + f_max) / 2.0;
            double df  = std::max((f_max - f_min) / 2.0, 1e-15);

            Eigen::VectorXd sX_norm = (sX.array() - x_c) / dx;
            Eigen::VectorXd sY_norm = (sY.array() - y_c) / dy;
            Eigen::VectorXd sF_norm = (sF.array() - f_c) / df;

            double max_abs_F = std::max(1e-12, sF.cwiseAbs().maxCoeff());

            BivariateBarycentricRational r;
            r.x_c = x_c; r.dx = dx;
            r.y_c = y_c; r.dy = dy;
            r.f_c = f_c; r.df = df;

            r.lambda.resize(1); r.lambda[0] = sX_norm[0];
            r.mu.resize(1);     r.mu[0] = sY_norm[0];
            r.alpha.resize(1, 1); r.alpha(0, 0) = 1.0;
            r.beta.resize(1, 1);  r.beta(0, 0) = sF_norm[0];

            std::vector<double> lambda_list;
            std::vector<double> mu_list;

            double error = 1.0;
            int iter = 0;

            while (error > tol && iter < max_iters) {
                int idx_max = 0;
                double max_err = -1.0;
                for (int p = 0; p < valid_count; ++p) {
                    double approx_val = r.evaluate(sX[p], sY[p]);
                    double curr_err = 0.0;
                    if (!std::isnan(approx_val) && !std::isinf(approx_val)) {
                        curr_err = std::abs(sF[p] - approx_val);
                    }
                    if (curr_err > max_err) {
                        max_err = curr_err;
                        idx_max = p;
                    }
                }

                double lambda_star = sX_norm[idx_max];
                double mu_star = sY_norm[idx_max];

                auto add_unique = [](std::vector<double>& vec, double val) {
                    auto it = std::find_if(vec.begin(), vec.end(), [val](double v) {
                        return std::abs(v - val) < 1e-14;
                    });
                    if (it == vec.end()) {
                        vec.push_back(val);
                        return true;
                    }
                    return false;
                };

                bool added_lam = add_unique(lambda_list, lambda_star);
                bool added_mu  = add_unique(mu_list, mu_star);

                if (!added_lam && !added_mu) break;

                const int n = static_cast<int>(lambda_list.size());
                const int m = static_cast<int>(mu_list.size());
                const int nm = n * m;

                std::vector<int> constrained_indices;
                std::vector<double> interp_values;

                for (int i = 0; i < n; ++i) {
                    for (int j = 0; j < m; ++j) {
                        for (int p = 0; p < valid_count; ++p) {
                            if (std::abs(sX_norm[p] - lambda_list[i]) < 1e-12 &&
                                std::abs(sY_norm[p] - mu_list[j]) < 1e-12)
                            {
                                constrained_indices.push_back(i + j * n);
                                interp_values.push_back(sF_norm[p]);
                                break;
                            }
                        }
                    }
                }

                const int k_interp = static_cast<int>(constrained_indices.size());
                const int u_count = nm - k_interp;

                std::vector<int> unconstrained_indices;
                for (int idx = 0; idx < nm; ++idx) {
                    if (std::find(constrained_indices.begin(), constrained_indices.end(), idx) == constrained_indices.end()) {
                        unconstrained_indices.push_back(idx);
                    }
                }

                Eigen::MatrixXd C_lam(n, valid_count);
                for (int p = 0; p < valid_count; ++p) {
                    double xp = sX_norm[p];
                    int match_idx = -1;
                    for (int i = 0; i < n; ++i) {
                        if (std::abs(xp - lambda_list[i]) < 1e-14) {
                            match_idx = i;
                            break;
                        }
                    }
                    if (match_idx != -1) {
                        C_lam.col(p).setZero();
                        C_lam(match_idx, p) = 1.0;
                    } else {
                        for (int i = 0; i < n; ++i) {
                            C_lam(i, p) = 1.0 / (xp - lambda_list[i]);
                        }
                    }
                }

                Eigen::MatrixXd C_mu(m, valid_count);
                for (int p = 0; p < valid_count; ++p) {
                    double yp = sY_norm[p];
                    int match_idx = -1;
                    for (int j = 0; j < m; ++j) {
                        if (std::abs(yp - mu_list[j]) < 1e-14) {
                            match_idx = j;
                            break;
                        }
                    }
                    if (match_idx != -1) {
                        C_mu.col(p).setZero();
                        C_mu(match_idx, p) = 1.0;
                    } else {
                        for (int j = 0; j < m; ++j) {
                            C_mu(j, p) = 1.0 / (yp - mu_list[j]);
                        }
                    }
                }

                Eigen::MatrixXd G(valid_count, nm);
                for (int p = 0; p < valid_count; ++p) {
                    for (int i = 0; i < n; ++i) {
                        for (int j = 0; j < m; ++j) {
                            G(p, i + j * n) = C_lam(i, p) * C_mu(j, p);
                        }
                    }
                }

                Eigen::MatrixXd M1(valid_count, nm);
                for (int p = 0; p < valid_count; ++p) {
                    for (int idx = 0; idx < nm; ++idx) {
                        double g_val = G(p, idx);
                        double d_scaled = sF_norm[p] * g_val;

                        double h_scaled = 0.0;
                        auto it = std::find(constrained_indices.begin(), constrained_indices.end(), idx);
                        if (it != constrained_indices.end()) {
                            int r_pos = static_cast<int>(std::distance(constrained_indices.begin(), it));
                            h_scaled = g_val * interp_values[r_pos];
                        }
                        M1(p, idx) = d_scaled - h_scaled;
                    }
                }

                Eigen::MatrixXd M2(valid_count, u_count);
                for (int s = 0; s < u_count; ++s) {
                    int unconstrained_idx = unconstrained_indices[s];
                    M2.col(s) = -G.col(unconstrained_idx);
                }

                Eigen::MatrixXd M_matrix;
                if (u_count > 0) {
                    M_matrix.resize(valid_count, nm + u_count);
                    M_matrix.leftCols(nm) = M1;
                    M_matrix.rightCols(u_count) = M2;
                } else {
                    M_matrix = M1;
                }

                Eigen::BDCSVD<Eigen::MatrixXd> svd(M_matrix, Eigen::ComputeThinV);
                Eigen::VectorXd v_solution = svd.matrixV().col(M_matrix.cols() - 1);

                Eigen::VectorXd alpha_vec = v_solution.head(nm);
                Eigen::VectorXd beta_u = v_solution.tail(u_count);

                Eigen::VectorXd beta_vec(nm);
                for (int idx = 0; idx < nm; ++idx) {
                    auto it = std::find(constrained_indices.begin(), constrained_indices.end(), idx);
                    if (it != constrained_indices.end()) {
                        int r_pos = static_cast<int>(std::distance(constrained_indices.begin(), it));
                        beta_vec[idx] = alpha_vec[idx] * interp_values[r_pos];
                    } else {
                        auto it_u = std::find(unconstrained_indices.begin(), unconstrained_indices.end(), idx);
                        int s_pos = static_cast<int>(std::distance(unconstrained_indices.begin(), it_u));
                        beta_vec[idx] = beta_u[s_pos];
                    }
                }

                r.lambda.resize(n);
                for (int i = 0; i < n; ++i) r.lambda[i] = lambda_list[i];

                r.mu.resize(m);
                for (int j = 0; j < m; ++j) r.mu[j] = mu_list[j];

                r.alpha = Eigen::Map<Eigen::MatrixXd>(alpha_vec.data(), n, m);
                r.beta  = Eigen::Map<Eigen::MatrixXd>(beta_vec.data(), n, m);

                double max_diff = 0.0;
                for (int p = 0; p < valid_count; ++p) {
                    double approx_val = r.evaluate(sX[p], sY[p]);
                    double diff = 0.0;
                    if (!std::isnan(approx_val) && !std::isinf(approx_val)) {
                        diff = std::abs(sF[p] - approx_val);
                    }
                    max_diff = std::max(max_diff, diff);
                }
                error = max_diff / max_abs_F;

                iter++;
            }

            return r;
        }
    };

    /**
     * @brief 还原出来的“有理-对数复合函数组合表达式”
     * 公式为: f(x, y) = R_barycentric(x, y) + c * ln(dir * (x*cos(theta) + y*sin(theta) - d0))
     */
    struct BivariateHybridRationalLog
    {
        BivariateBarycentricRational rational_part; // 纯有理多项式比值部分

        double c = 0.0;        // 对数强度系数 C
        double theta = 0.0;    // 渐近线法向旋转角 (弧度值)
        double d0 = 0.0;       // 渐近线偏离原点的正交距离
        int dir = 1;           // 渐近线方向指示因子: 1 代表正向, -1 代表负向

        double evaluate(double x, double y) const
        {
            double r_val = rational_part.evaluate(x, y);
            if (std::abs(c) < 1e-9) {
                return r_val;
            }

            // 计算法向投影距离
            double u = x * std::cos(theta) + y * std::sin(theta);
            double val = (dir == 1) ? (u - d0) : (d0 - u);

            // 带有紧凑安全缓冲带宽，阻隔复数域 NaN
            if (val > 1e-15) {
                return r_val + c * std::log(val);
            }
            return r_val;
        }

        double operator()(double x, double y) const
        {
            return evaluate(x, y);
        }
    };

    /**
     * @brief 👑 2026 顶尖数值分析算法: 任意法向对数奇点自适应探测与剥离拟合器
     */
    class ScatteredHybridPaaa
    {
    public:
        static BivariateHybridRationalLog fit(
            const FfiFunction<double(double, double)>& f,
            const Eigen::VectorXd& X,
            const Eigen::VectorXd& Y,
            double tol = 1e-9,
            int max_iters = 50)
        {
            const int K = static_cast<int>(X.size());

            // 1. 数据安全过滤
            std::vector<double> valid_X, valid_Y, valid_F;
            valid_X.reserve(K); valid_Y.reserve(K); valid_F.reserve(K);

            for (int p = 0; p < K; ++p) {
                double xp = X[p]; double yp = Y[p];
                if (std::isnan(xp) || std::isinf(xp) || std::isnan(yp) || std::isinf(yp)) continue;
                double fp = f(xp, yp);
                if (std::isnan(fp) || std::isinf(fp)) continue;

                valid_X.push_back(xp);
                valid_Y.push_back(yp);
                valid_F.push_back(fp);
            }

            int valid_count = static_cast<int>(valid_X.size());
            if (valid_count == 0) {
                BivariateHybridRationalLog empty_model;
                return empty_model;
            }

            Eigen::VectorXd sX(valid_count);
            Eigen::VectorXd sY(valid_count);
            Eigen::VectorXd sF(valid_count);
            for (int i = 0; i < valid_count; ++i) {
                sX[i] = valid_X[i];
                sY[i] = valid_Y[i];
                sF[i] = valid_F[i];
            }

            // ========================================================
            // 💡 修复绝对值掩蔽 Bug: 引入无监督“最近邻差分梯度”
            // ========================================================
            std::vector<double> slopes(valid_count, 0.0);
            for (int i = 0; i < valid_count; ++i) {
                double min_dist = 1e30;
                int nn_idx = -1;
                for (int j = 0; j < valid_count; ++j) {
                    if (i == j) continue;
                    double d = std::sqrt((sX[i] - sX[j])*(sX[i] - sX[j]) + (sY[i] - sY[j])*(sY[i] - sY[j]));
                    if (d < min_dist) {
                        min_dist = d;
                        nn_idx = j;
                    }
                }
                if (nn_idx != -1 && min_dist > 1e-15) {
                    // 有限差分估算该点的局部梯度幅值
                    slopes[i] = std::abs(sF[i] - sF[nn_idx]) / min_dist;
                }
            }

            struct PointInfo {
                double x, y, f;
                double slope;
            };
            std::vector<PointInfo> pts(valid_count);
            for (int i = 0; i < valid_count; ++i) {
                pts[i] = {sX[i], sY[i], sF[i], slopes[i]};
            }
            // 💡 关键改动：按照局部梯度幅值降序排列，筛选出真正处于渐近线奇点边缘的点！
            std::sort(pts.begin(), pts.end(), [](const PointInfo& a, const PointInfo& b) {
                return a.slope > b.slope;
            });

            // 提取最强梯度响应点集 S_sing (对数渐近线在这里彻底压制常数偏移量)
            int Ns = std::max(5, static_cast<int>(valid_count * 0.35));
            std::vector<double> sX_sing(Ns), sY_sing(Ns), sF_sing(Ns);
            for (int i = 0; i < Ns; ++i) {
                sX_sing[i] = pts[i].x;
                sY_sing[i] = pts[i].y;
                sF_sing[i] = pts[i].f;
            }

            // ========================================================
            // 💡 无梯度、全自动、任意倾斜角 \theta 奇点大扫描
            // ========================================================
            double best_R2 = -1.0;
            double best_theta = 0.0;
            double best_d0 = 0.0;
            double best_C = 0.0;
            int best_dir = 1;

            const double PI = 3.14159265358979323846;
            int theta_steps = 36; // 每次旋转 5 度进行高频检测

            for (int t = 0; t < theta_steps; ++t) {
                double theta = (t * PI) / theta_steps;
                double cos_t = std::cos(theta);
                double sin_t = std::sin(theta);

                // 计算投影距离
                std::vector<double> u(Ns);
                double u_min = 1e30;
                double u_max = -1e30;
                for (int i = 0; i < Ns; ++i) {
                    u[i] = sX_sing[i] * cos_t + sY_sing[i] * sin_t;
                    u_min = std::min(u_min, u[i]);
                    u_max = std::max(u_max, u[i]);
                }
                double u_range = std::max(1e-9, u_max - u_min);

                // 候选边界紧凑微元集
                std::vector<double> eps_candidates = {0.001 * u_range, 0.01 * u_range, 0.05 * u_range};
                for (double eps : eps_candidates) {

                    // 场景一: 正向渐近线 (u > d0)
                    {
                        double d0 = u_min - eps;
                        std::vector<double> L(Ns);
                        bool valid = true;
                        for (int i = 0; i < Ns; ++i) {
                            double val = u[i] - d0;
                            if (val <= 1e-15) { valid = false; break; }
                            L[i] = std::log(val);
                        }
                        if (valid) {
                            // 执行线性最小二乘 (1D 线性回归) 拟合对数强度系数
                            double sum_L = 0.0, sum_Y = 0.0;
                            for (int i = 0; i < Ns; ++i) { sum_L += L[i]; sum_Y += sF_sing[i]; }
                            double mean_L = sum_L / Ns;
                            double mean_Y = sum_Y / Ns;

                            double num = 0.0, den = 0.0;
                            for (int i = 0; i < Ns; ++i) {
                                num += (L[i] - mean_L) * (sF_sing[i] - mean_Y);
                                den += (L[i] - mean_L) * (L[i] - mean_L);
                            }
                            if (den > 1e-15) {
                                double C = num / den;
                                double D_const = mean_Y - C * mean_L;

                                // 计算决定系数 R2 判断相似度
                                double ss_res = 0.0, ss_tot = 0.0;
                                for (int i = 0; i < Ns; ++i) {
                                    double pred = C * L[i] + D_const;
                                    ss_res += (sF_sing[i] - pred) * (sF_sing[i] - pred);
                                    ss_tot += (sF_sing[i] - mean_Y) * (sF_sing[i] - mean_Y);
                                }
                                double R2 = (ss_tot > 1e-15) ? (1.0 - ss_res / ss_tot) : 0.0;
                                if (R2 > best_R2) {
                                    best_R2 = R2;
                                    best_theta = theta;
                                    best_d0 = d0;
                                    best_C = C;
                                    best_dir = 1;
                                }
                            }
                        }
                    }

                    // 场景二: 负向渐近线 (u < d0)
                    {
                        double d0 = u_max + eps;
                        std::vector<double> L(Ns);
                        bool valid = true;
                        for (int i = 0; i < Ns; ++i) {
                            double val = d0 - u[i];
                            if (val <= 1e-15) { valid = false; break; }
                            L[i] = std::log(val);
                        }
                        if (valid) {
                            double sum_L = 0.0, sum_Y = 0.0;
                            for (int i = 0; i < Ns; ++i) { sum_L += L[i]; sum_Y += sF_sing[i]; }
                            double mean_L = sum_L / Ns;
                            double mean_Y = sum_Y / Ns;

                            double num = 0.0, den = 0.0;
                            for (int i = 0; i < Ns; ++i) {
                                num += (L[i] - mean_L) * (sF_sing[i] - mean_Y);
                                den += (L[i] - mean_L) * (L[i] - mean_L);
                            }
                            if (den > 1e-15) {
                                double C = num / den;
                                double D_const = mean_Y - C * mean_L;

                                double ss_res = 0.0, ss_tot = 0.0;
                                for (int i = 0; i < Ns; ++i) {
                                    double pred = C * L[i] + D_const;
                                    ss_res += (sF_sing[i] - pred) * (sF_sing[i] - pred);
                                    ss_tot += (sF_sing[i] - mean_Y) * (sF_sing[i] - mean_Y);
                                }
                                double R2 = (ss_tot > 1e-15) ? (1.0 - ss_res / ss_tot) : 0.0;
                                if (R2 > best_R2) {
                                    best_R2 = R2;
                                    best_theta = theta;
                                    best_d0 = d0;
                                    best_C = C;
                                    best_dir = -1;
                                }
                            }
                        }
                    }

                }
            }

            // ========================================================
            // 💡 决策机制: 若对数拟合 R2 高于 0.85，说明确实有对数渐近线，直接一键剥离！
            // ========================================================
            double final_C = 0.0;
            double final_theta = 0.0;
            double final_d0 = 0.0;
            int final_dir = 1;

            if (best_R2 > 0.85) {
                final_C = best_C;
                final_theta = best_theta;
                final_d0 = best_d0;
                final_dir = best_dir;
            }

            // 建立残差黑盒进行拟合
            auto f_residue_lambda = [&](double x, double y) -> double {
                double val = f(x, y);
                if (std::isnan(val) || std::isinf(val)) return val;

                if (std::abs(final_C) > 1e-9) {
                    double u = x * std::cos(final_theta) + y * std::sin(final_theta);
                    double term_val = (final_dir == 1) ? (u - final_d0) : (final_d0 - u);
                    if (term_val > 1e-15) {
                        val -= final_C * std::log(term_val); // 解析剥离
                    }
                }
                return val;
            };

            FfiFunction<double(double, double)> f_residue(f_residue_lambda);

            // 让原版 ScatteredPaaa 去解决被剥离后光滑得如同白纸的残差函数！
            BivariateBarycentricRational best_rational = ScatteredPaaa::fit(f_residue, sX, sY, tol, max_iters);

            BivariateHybridRationalLog result;
            result.rational_part = best_rational;
            result.c = final_C;
            result.theta = final_theta;
            result.d0 = final_d0;
            result.dir = final_dir;

            return result;
        }
    };
} // namespace StuCanvas::utils