// -*- LSST-C++ -*-
/**
 * @file   Quadrature.cc
 * @author S. Bickerton
 * @date   May 25, 2009
 *
 * This test evaluates the romberg 1D and 2D
 * integrators in the afw::math (Quadrature) suite.
 */
#include <cmath>
#include <vector>

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE Quadrature

#include "boost/test/unit_test.hpp"

#include "lsst/afw/math/Quadrature.h"

using namespace std;
namespace math = lsst::afw::math;


/* define a simple 1D function as a functor to be integrated.
 * I've chosen a parabola here: f(x) = K + kx*x*x
 * as it's got an easy-to-check analytic answer.
 *
 * We have to inherit from IntegrandBase for the integrator to work.
 */
template<typename IntegrandT>
class Parab1D : public math::IntegrandBase {
public:
    Parab1D(double K, double kx) : _K(K), _kx(kx) {}
    
    // for this example we have an analytic answer to check
    double getAnalyticArea(double const x1, double const x2) { return _K*(x2-x1) - _kx*(x2*x2*x2-x1*x1*x1)/3.0; }
    
    // operator() must be overloaded to return the evaluation of the function
    IntegrandT operator() (IntegrandT const x) { return (_K - _kx*x*x); }
    
private:
    double _K, _kx;
};

/* define a simple 2D function as a functor to be integrated.
 * I've chosen a 2D paraboloid: f(x) = K - kx*x*x - ky*y*y
 * as it's got an easy-to-check analytic answer.
 *
 * Note that we have to inherit from IntegrandBase
 */
template<typename IntegrandT>
class Parab2D : public math::IntegrandBase {
public:
    Parab2D(double K, double kx, double ky) : _K(K), _kx(kx), _ky(ky) {}
    
    // for this example we have an analytic answer to check
    double getAnalyticVolume(double const x1, double const x2, double const y1, double const y2) {
        double const xw = x2 - x1;
        double const yw = y2 - y1;
        return _K*xw*yw - _kx*(x2*x2*x2-x1*x1*x1)*yw/3.0 - _ky*(y2*y2*y2-y1*y1*y1)*xw/3.0;
    }
    
    // operator() must be overloaded to return the evaluation of the function
    IntegrandT operator() (IntegrandT const x) { return (_K - _kx*x*x - _ky*_y*_y); }
    
private:
    double _K, _kx, _ky;
    using IntegrandBase::_y;
};


/**
 * @brief Test the 1D integrator on a Parabola
 * @note default precision is 1e-6 for romberg()
 */
BOOST_AUTO_TEST_CASE(Parabola1D) {

    // set limits of integration
    double x1 = 0, x2 = 9;
    // set the coefficients for the quadratic equation (parabola f(x) = K + kx*x*x)
    double K = 100, kx = 1.0;

    // ==========   The 1D integrator ==========
    // instantiate a Parab1D Functor, integrate numerically, and analytically
    Parab1D<double> parab1d(K, kx);
    double parab_area_romberg  = math::romberg<math::IntegrandBase>(parab1d, x1, x2);
    double parab_area_analytic = parab1d.getAnalyticArea(x1, x2);

    BOOST_CHECK_CLOSE(parab_area_romberg, parab_area_analytic, 1e-6);
}


/**
 * @brief Test the 2D integrator on a Paraboloid
 * @note default precision is 1e-6 from romberg2D()
 */
BOOST_AUTO_TEST_CASE(Parabola2D) {

    // set limits of integration
    double x1 = 0, x2 = 9, y1 = 0, y2 = 9;
    // set the coefficients for the quadratic equation (parabola f(x) = K + kx*x*x + ky*y*y)
    double K = 100, kx = 1.0, ky = 1.0;

    // ==========   The 2D integrator ==========
    // instantiate a Parab2D, integrate numerically and analytically
    Parab2D<double> parab2d(K, kx, ky);
    double parab_volume_romberg  = math::romberg2D(parab2d, x1, x2, y1, y2);
    double parab_volume_analytic = parab2d.getAnalyticVolume(x1, x2, y1, y2);
    
    BOOST_CHECK_CLOSE(parab_volume_romberg, parab_volume_analytic, 1e-6);
}

