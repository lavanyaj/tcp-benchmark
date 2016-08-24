#ifndef __ranvar_h
#define __ranvar_h

#include <sys/time.h>

#define INTER_DISCRETE 0	// no interpolation (discrete)
#define INTER_CONTINUOUS 1	// linear interpolation
#define INTER_INTEGRAL 2	// linear interpolation and round up

struct CDFentry {
	double cdf_;
	double val_;
};

class EmpiricalRandomVariable {
public:
	virtual double value(double u);
	virtual double interpolate(double u, double x1, double y1, double x2, double y2);
	virtual double avg();
	EmpiricalRandomVariable(int interp=INTER_DISCRETE);
	~EmpiricalRandomVariable();
	int loadCDF(const char* filename);

protected:
	int lookup(double u);

	int interpolation_;	// how to interpolate data (INTER_DISCRETE...)
	int numEntry_;		// number of entries in the CDF table
	int maxEntry_;		// size of the CDF table (mem allocation)
	CDFentry* table_;	// CDF table of (val_, cdf_)
};




#endif
