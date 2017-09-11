#define CATCH_CONFIG_MAIN
#include <catch.hpp>

#include <tropter/tropter.h>

#include "testing.h"

using Eigen::Ref;
using Eigen::VectorXd;
using Eigen::RowVectorXd;
using Eigen::Vector2d;
using Eigen::Vector4d;
using Vector5d = Eigen::Matrix<double, 5, 1>;
using Eigen::MatrixXd;

using namespace tropter;

template<typename T>
class HS071 : public OptimizationProblem<T> {
public:
    HS071() : OptimizationProblem<T>(4, 2) {
        this->set_variable_bounds(Vector4d(1, 1, 1, 1), Vector4d(5, 5, 5, 5));
        this->set_constraint_bounds(Vector2d(25, 40), Vector2d(2e19, 40.0));
    }
    void calc_objective(const VectorX<T>& x, T& obj_value) const override {
        obj_value = x[0] * x[3] * (x[0] + x[1] + x[2]) + x[2];
    }
    void calc_constraints(
            const VectorX<T>& x, Eigen::Ref<VectorX<T>> constr) const override {
        constr[0] = x.prod();
        constr[1] = x.squaredNorm();
    }
    void analytical_gradient(const VectorXd& x,
            Eigen::Ref<VectorXd> grad) const {
        assert(grad.size() == this->get_num_variables());
        grad[0] = x[3] * (x[0] + x[1] + x[2]) + x[0] * x[3];
        grad[1] = x[0] * x[3];
        grad[2] = x[0] * x[3] + 1;
        grad[3] = x[0] * (x[0] + x[1] + x[2]);
    }
    void analytical_jacobian(const VectorXd& x,
            Eigen::Ref<MatrixXd> jacobian) {
        assert(jacobian.rows() == this->get_num_constraints());
        assert(jacobian.cols() == this->get_num_variables());
        jacobian(0, 0) = x[1] * x[2] * x[3];
        jacobian(0, 1) = x[0] * x[2] * x[3];
        jacobian(0, 2) = x[0] * x[1] * x[3];
        jacobian(0, 3) = x[0] * x[1] * x[2];

        jacobian(1, 0) = 2 * x[0];
        jacobian(1, 1) = 2 * x[1];
        jacobian(1, 2) = 2 * x[2];
        jacobian(1, 3) = 2 * x[3];
    }

    /// Only the upper right triangle is filled in.
    void analytical_hessian_lagrangian(const VectorXd& x,
            const double& obj_factor,
            const VectorXd& lambda,
            Eigen::Ref<MatrixXd> hessian) {
        assert(hessian.rows() == this->get_num_variables());
        assert(hessian.cols() == this->get_num_variables());
        // obj_factor * grad^2 f(x)
        hessian(0, 0) = obj_factor * (2 * x[3]);
        hessian(0, 1) = obj_factor * (x[3]);
        hessian(1, 1) = 0;
        hessian(0, 2) = obj_factor * (x[3]);
        hessian(1, 2) = 0;
        hessian(2, 2) = 0;
        hessian(0, 3) = obj_factor * (2 * x[0] + x[1] + x[2]);
        hessian(1, 3) = obj_factor * (x[0]);
        hessian(2, 3) = obj_factor * (x[0]);
        hessian(3, 3) = 0;

        // lambda_0 * grad^2 g_0(x)
        hessian(0, 1) += lambda[0] * (x[2] * x[3]);
        hessian(0, 2) += lambda[0] * (x[1] * x[3]);
        hessian(1, 2) += lambda[0] * (x[0] * x[3]);
        hessian(0, 3) += lambda[0] * (x[1] * x[2]);
        hessian(1, 3) += lambda[0] * (x[0] * x[2]);
        hessian(2, 3) += lambda[0] * (x[0] * x[1]);

        // lambda_1 * grad^2 g_1(x)
        hessian(0, 0) += lambda[1] * 2;
        hessian(1, 1) += lambda[1] * 2;
        hessian(2, 2) += lambda[1] * 2;
        hessian(3, 3) += lambda[1] * 2;
    }
};

//TEST_CASE("Check finite differences with analytical deriv.", "[finitediff]")
//{
//    HS071<double> problem;
//    VectorXd x(problem.get_num_variables());
//    x << 1.5, 1.6, 1.7, 1.8;
//    VectorXd gradient;
//    problem.analytical_gradient(x, gradient);
//    std::cout << "DEBUG " << gradient << std::endl;
//}

TEST_CASE("Check derivatives with analytical deriv.")
{
    HS071<adouble> problem;
    VectorXd x(problem.get_num_variables());
    x << 1.5, 1.6, 1.7, 1.8;

    // Analytical derivatives.
    // -----------------------

    // Gradient.
    VectorXd analytical_gradient(problem.get_num_variables());
    problem.analytical_gradient(x, analytical_gradient);
    // Hessian.
    MatrixXd analytical_hessian(problem.get_num_variables(),
            problem.get_num_variables());
    const double obj_factor = 1.0;
    VectorXd lambda(problem.get_num_constraints()); lambda << 0.5, 1.5;
    problem.analytical_hessian_lagrangian(x, obj_factor, lambda,
            analytical_hessian);
    // Jacobian.
    MatrixXd analytical_jacobian(problem.get_num_constraints(),
            problem.get_num_variables());
    problem.analytical_jacobian(x, analytical_jacobian);

    // Finite differences.
    // -------------------
    SECTION("Finite differences") {
        HS071<double> problemd;
        auto decorator = problemd.make_decorator();
        // Must first initialize.
        std::vector<unsigned int> jacobian_row_indices;
        std::vector<unsigned int> jacobian_col_indices;
        std::vector<unsigned int> hessian_row_indices;
        std::vector<unsigned int> hessian_col_indices;
        decorator->calc_sparsity(decorator->make_initial_guess_from_bounds(),
                jacobian_row_indices, jacobian_col_indices,
                hessian_row_indices, hessian_col_indices);

        // Gradient.
        VectorXd fd_gradient(problem.get_num_variables());
        decorator->calc_gradient(problem.get_num_variables(), x.data(), false,
                fd_gradient.data());
        TROPTER_REQUIRE_EIGEN(analytical_gradient, fd_gradient, 1e-8);

        // Hessian (of the Lagrangian).
        // TODO

        // Jacobian.
        const auto num_jacobian_elem = problem.get_num_constraints() *
                problem.get_num_variables();
        // The Jacobian is dense.
        REQUIRE(jacobian_row_indices.size() == num_jacobian_elem);
        REQUIRE(jacobian_col_indices.size() == num_jacobian_elem);
        VectorXd fd_jacobian_values(num_jacobian_elem);
        decorator->calc_jacobian(problem.get_num_variables(), x.data(), false,
                num_jacobian_elem, fd_jacobian_values.data());
        INFO(analytical_jacobian);
        INFO(fd_jacobian_values);
        for (int inz = 0; inz < (int)num_jacobian_elem; ++inz) {
            const auto& i = jacobian_row_indices[inz];
            const auto& j = jacobian_col_indices[inz];
            REQUIRE(analytical_jacobian(i, j) ==
                    Approx(fd_jacobian_values[inz]).epsilon(1e-8));
        }
    }

    // Automatic derivatives.
    // ----------------------
    SECTION("ADOL-C") {

        auto decorator = problem.make_decorator();
        // Must first initialize the ADOL-C tapes.
        std::vector<unsigned int> jacobian_row_indices;
        std::vector<unsigned int> jacobian_col_indices;
        std::vector<unsigned int> hessian_row_indices;
        std::vector<unsigned int> hessian_col_indices;
        decorator->calc_sparsity(decorator->make_initial_guess_from_bounds(),
                jacobian_row_indices, jacobian_col_indices,
                hessian_row_indices, hessian_col_indices);

        // Gradient.
        VectorXd adolc_gradient(problem.get_num_variables());
        decorator->calc_gradient(problem.get_num_variables(), x.data(), false,
                adolc_gradient.data());
        TROPTER_REQUIRE_EIGEN(analytical_gradient, adolc_gradient, 1e-16);

        // Hessian (of the Lagrangian).
        const unsigned num_hessian_nonzeros =
                (unsigned)hessian_row_indices.size();
        VectorXd adolc_hessian_values(num_hessian_nonzeros);
        decorator->calc_hessian_lagrangian(problem.get_num_variables(),
                x.data(), false,
                obj_factor, problem.get_num_constraints(), lambda.data(), false,
                num_hessian_nonzeros, adolc_hessian_values.data());
        for (int inz = 0; inz < (int)num_hessian_nonzeros; ++inz) {
            const auto& i = hessian_row_indices[inz];
            const auto& j = hessian_col_indices[inz];
            REQUIRE(analytical_hessian(i, j) == adolc_hessian_values[inz]);
        }

        // Jacobian.
        const auto num_jacobian_elem = problem.get_num_constraints() *
                problem.get_num_variables();
        // The Jacobian is dense.
        REQUIRE(jacobian_row_indices.size() == num_jacobian_elem);
        REQUIRE(jacobian_col_indices.size() == num_jacobian_elem);
        VectorXd adolc_jacobian_values(num_jacobian_elem);
        decorator->calc_jacobian(problem.get_num_variables(), x.data(), false,
                num_jacobian_elem, adolc_jacobian_values.data());
        for (int inz = 0; inz < (int)num_jacobian_elem; ++inz) {
            const auto& i = jacobian_row_indices[inz];
            const auto& j = jacobian_col_indices[inz];
            REQUIRE(analytical_jacobian(i, j) == adolc_jacobian_values[inz]);
        }
    }
}

template<typename T>
class SparseJacobian : public OptimizationProblem<T> {
public:
    SparseJacobian() : OptimizationProblem<T>(4, 5) {
        this->set_variable_bounds(Vector4d(1, 1, 1, 1), Vector4d(5, 5, 5, 5));
        this->set_constraint_bounds(VectorXd::Ones(5) * -2e19,
                                    VectorXd::Ones(5) *  2e19);
    }
    void calc_objective(const VectorX<T>& x, T& obj_value) const override {
        obj_value = x.squaredNorm();
    }
    void calc_constraints(
            const VectorX<T>& x, Eigen::Ref<VectorX<T>> constr) const override {
        const int m = this->get_num_constraints();
        const int n = (int)x.size();
        constr.setZero();
        // Sparsity pattern (and order of jacobian_values).
        // 0 . . .
        // 1 2 . .
        // . 3 4 .
        // . . 5 6
        // . . . 7
        for (int i = 0; i < m; ++i) {
            for (int j = std::max(i - 1, 0); j < std::min(i + 1, n); ++j) {
                constr[i] += x[j] * x[j];
            }
        }
    }
    void analytical_gradient(const VectorXd& x,
            Eigen::Ref<VectorXd> grad) const {
        assert(grad.size() == this->get_num_variables());
        grad.setZero();
        for (int i = 0; i < grad.size(); ++i) {
            grad[i] = 2 * x[i];
        }
    }
    void analytical_jacobian(const VectorXd& x,
            Eigen::Ref<MatrixXd> jacobian) {
        const int m = this->get_num_constraints();
        const int n = (int)x.size();
        assert(jacobian.rows() == m);
        assert(jacobian.cols() == n);
        jacobian.setZero();
        for (int i = 0; i < m; ++i) {
            for (int j = std::max(i - 1, 0); j < std::min(i + 1, n); ++j) {
                jacobian(i, j) += 2 * x[j];
            }
        }
    }

    /// Only the upper right triangle is filled in.
    void analytical_hessian_lagrangian(const VectorXd& x,
            const double& obj_factor,
            const VectorXd& lambda,
            Eigen::Ref<MatrixXd> hessian) {
        assert(hessian.rows() == this->get_num_variables());
        assert(hessian.cols() == this->get_num_variables());
        // obj_factor * grad^2 f(x)
        hessian.setZero();
        for (int i = 0; i < (int)this->get_num_variables(); ++i) {
            hessian(i, i) = obj_factor * 2;
        }

        // lambda_j * grad^2 g_j(x)
        const int m = this->get_num_constraints();
        const int n = (int)x.size();
        for (int i = 0; i < m; ++i) {
            for (int j = std::max(i - 1, 0); j < std::min(i + 1, n); ++j) {
                hessian(j, j) += lambda[i] * 2;
            }
        }
    }
};

TEST_CASE("Check derivatives with analytical deriv.; sparse Jacobian.")
{
    SparseJacobian<adouble> problem;
    VectorXd x(problem.get_num_variables());
    x << 3.1, -1.5, -0.25, 5.3;

    // Analytical derivatives.
    // -----------------------

    // Gradient.
    VectorXd analytical_gradient(problem.get_num_variables());
    problem.analytical_gradient(x, analytical_gradient);
    // Hessian.
    MatrixXd analytical_hessian(problem.get_num_variables(),
            problem.get_num_variables());
    const double obj_factor = 1.0;
    VectorXd lambda(problem.get_num_constraints());
    lambda << 0.5, 1.5, 2.5, 3.0, 0.19;
    problem.analytical_hessian_lagrangian(x, obj_factor, lambda,
            analytical_hessian);
    // Jacobian.
    MatrixXd analytical_jacobian(problem.get_num_constraints(),
            problem.get_num_variables());
    problem.analytical_jacobian(x, analytical_jacobian);
    const auto num_jacobian_elem = 2 * problem.get_num_variables();

    // Finite differences.
    // -------------------
    SECTION("Finite differences") {
        SparseJacobian<double> problemd;
        auto proxy = problemd.make_decorator();
        // Must first initialize.
        std::vector<unsigned int> jacobian_row_indices;
        std::vector<unsigned int> jacobian_col_indices;
        std::vector<unsigned int> hessian_row_indices;
        std::vector<unsigned int> hessian_col_indices;
        proxy->calc_sparsity(proxy->make_initial_guess_from_bounds(),
                jacobian_row_indices, jacobian_col_indices,
                hessian_row_indices, hessian_col_indices);

        // Gradient.
        VectorXd fd_gradient(problem.get_num_variables());
        proxy->calc_gradient(problem.get_num_variables(), x.data(), false,
                fd_gradient.data());
        INFO(analytical_gradient);
        INFO(fd_gradient);
        TROPTER_REQUIRE_EIGEN(analytical_gradient, fd_gradient, 1e-7);

        // Hessian (of the Lagrangian).
        // TODO

        // Jacobian.
        REQUIRE(jacobian_row_indices.size() == num_jacobian_elem);
        REQUIRE(jacobian_col_indices.size() == num_jacobian_elem);
        VectorXd fd_jacobian_values(num_jacobian_elem);
        proxy->calc_jacobian(problem.get_num_variables(), x.data(), false,
                num_jacobian_elem, fd_jacobian_values.data());
        INFO(analytical_jacobian);
        INFO(fd_jacobian_values);
        for (int inz = 0; inz < (int)num_jacobian_elem; ++inz) {
            const auto& i = jacobian_row_indices[inz];
            const auto& j = jacobian_col_indices[inz];
            REQUIRE(analytical_jacobian(i, j) ==
                    Approx(fd_jacobian_values[inz]).epsilon(1e-8));
        }
    }

    // Automatic derivatives.
    // ----------------------
    SECTION("ADOL-C") {

        auto proxy = problem.make_decorator();
        // Must first initialize the ADOL-C tapes.
        std::vector<unsigned int> jacobian_row_indices;
        std::vector<unsigned int> jacobian_col_indices;
        std::vector<unsigned int> hessian_row_indices;
        std::vector<unsigned int> hessian_col_indices;
        proxy->calc_sparsity(proxy->make_initial_guess_from_bounds(),
                jacobian_row_indices, jacobian_col_indices,
                hessian_row_indices, hessian_col_indices);

        // Gradient.
        VectorXd adolc_gradient(problem.get_num_variables());
        proxy->calc_gradient(problem.get_num_variables(), x.data(), false,
                adolc_gradient.data());
        TROPTER_REQUIRE_EIGEN(analytical_gradient, adolc_gradient, 1e-16);

        // Hessian (of the Lagrangian).
        const unsigned num_hessian_nonzeros =
                (unsigned)hessian_row_indices.size();
        VectorXd adolc_hessian_values(num_hessian_nonzeros);
        proxy->calc_hessian_lagrangian(problem.get_num_variables(), x.data(),
                false,
                obj_factor, problem.get_num_constraints(), lambda.data(), false,
                num_hessian_nonzeros, adolc_hessian_values.data());
        for (int inz = 0; inz < (int)num_hessian_nonzeros; ++inz) {
            const auto& i = hessian_row_indices[inz];
            const auto& j = hessian_col_indices[inz];
            REQUIRE(analytical_hessian(i, j) ==
                    Approx(adolc_hessian_values[inz]).epsilon(1e-15));
        }

        // Jacobian.
        REQUIRE(jacobian_row_indices.size() == num_jacobian_elem);
        REQUIRE(jacobian_col_indices.size() == num_jacobian_elem);
        VectorXd adolc_jacobian_values(num_jacobian_elem);
        proxy->calc_jacobian(problem.get_num_variables(), x.data(), false,
                num_jacobian_elem, adolc_jacobian_values.data());
        for (int inz = 0; inz < (int)num_jacobian_elem; ++inz) {
            const auto& i = jacobian_row_indices[inz];
            const auto& j = jacobian_col_indices[inz];
            REQUIRE(analytical_jacobian(i, j) == adolc_jacobian_values[inz]);
        }
    }
}

TEST_CASE("Check finite differences on bounds", "[finitediff][!mayfail]")
{
    HS071<adouble> problem;
    VectorXd x(problem.get_num_variables());
    x << 1, 5, 1, 5;

    // Preserve the integrity of the test by making sure that x acutally lies
    // on the bounds.
    REQUIRE(x[0] == problem.get_variable_lower_bounds()[0]);
    REQUIRE(x[1] == problem.get_variable_upper_bounds()[1]);
    REQUIRE(x[2] == problem.get_variable_lower_bounds()[2]);
    REQUIRE(x[3] == problem.get_variable_upper_bounds()[3]);
   // TODO

    throw std::runtime_error("Test not written yet.");
}

// TODO try x with a very different magnitude (x = 1000, x = 1e-4).






