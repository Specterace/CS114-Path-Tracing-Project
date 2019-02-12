#include <stdio.h>
#include <stdlib.h>
#include <cmath>
#include <random>
#include <vector>
#include <omp.h>
#define PI 3.1415926535897932384626433832795

//Path-tracing Version 1.1

/*
* Thread-safe random number generator
*/

struct RNG {
	RNG() : distrb(0.0, 1.0), engines() {}

	void init(int nworkers) {
		engines.resize(nworkers);
#if 0
		std::random_device rd;
		for (int i = 0; i < nworkers; ++i)
			engines[i].seed(rd());
#else
		std::seed_seq seq{ 1234 };
		std::vector<std::uint32_t> seeds(nworkers);
		seq.generate(seeds.begin(), seeds.end());
		for (int i = 0; i < nworkers; ++i) engines[i].seed(seeds[i]);
#endif
	}

	double operator()() {
		int id = omp_get_thread_num();
		return distrb(engines[id]);
	}

	std::uniform_real_distribution<double> distrb;
	std::vector<std::mt19937> engines;
} rng;


/*
* Basic data types
*/

struct Vec {
	double x, y, z;

	Vec(double x_ = 0, double y_ = 0, double z_ = 0) { x = x_; y = y_; z = z_; }

	Vec operator+ (const Vec &b) const { return Vec(x + b.x, y + b.y, z + b.z); }
	Vec operator- (const Vec &b) const { return Vec(x - b.x, y - b.y, z - b.z); }
	Vec operator* (double b) const { return Vec(x*b, y*b, z*b); }

	Vec mult(const Vec &b) const { return Vec(x*b.x, y*b.y, z*b.z); }
	Vec& normalize() { return *this = *this * (1.0 / std::sqrt(x*x + y*y + z*z)); }
	double dot(const Vec &b) const { return x*b.x + y*b.y + z*b.z; }
	Vec cross(const Vec&b) const { return Vec(y*b.z - z*b.y, z*b.x - x*b.z, x*b.y - y*b.x); }
};

struct Ray {
	Vec o, d;
	Ray(Vec o_, Vec d_) : o(o_), d(d_) {}
};

struct BRDF {
	virtual Vec eval(const Vec &n, const Vec &o, const Vec &i) const = 0;
	virtual void sample(const Vec &n, const Vec &o, Vec &i, double &pdf) const = 0;
};


/*
* Utility functions
*/

inline double clamp(double x) {
	return x < 0 ? 0 : x > 1 ? 1 : x;
}

inline int toInt(double x) {
	return static_cast<int>(std::pow(clamp(x), 1.0 / 2.2) * 255 + .5);
}


/*
* Shapes
*/

struct Sphere {
	Vec p, e;           // position, emitted radiance
	double rad;         // radius
	const BRDF &brdf;   // BRDF

	Sphere(double rad_, Vec p_, Vec e_, const BRDF &brdf_) :
		rad(rad_), p(p_), e(e_), brdf(brdf_) {}

	double intersect(const Ray &r) const { // returns distance, 0 if nohit
		Vec op = p - r.o; // Solve t^2*d.d + 2*t*(o-p).d + (o-p).(o-p)-R^2 = 0
		double t, eps = 1e-4, b = op.dot(r.d), det = b*b - op.dot(op) + rad*rad;
		if (det<0) return 0; else det = sqrt(det);
		return (t = b - det)>eps ? t : ((t = b + det)>eps ? t : 0);
	}
};


/*
* Sampling functions
*/

inline void createLocalCoord(const Vec &n, Vec &u, Vec &v, Vec &w) {
	w = n;
	u = ((std::abs(w.x)>.1 ? Vec(0, 1) : Vec(1)).cross(w)).normalize();
	v = w.cross(u);
}


/*
* BRDFs   (each had it's own structure)
*/

// Ideal diffuse BRDF
struct DiffuseBRDF : public BRDF {
	DiffuseBRDF(Vec kd_) : kd(kd_) {}

	Vec eval(const Vec &n, const Vec &o, const Vec &i) const {
		return kd * (1.0 / PI);
	}

	void sample(const Vec &n, const Vec &o, Vec &i, double &pdf) const {		//SAMPLE IMPLEMENTATION
		float z, r, x, y, phi;
		z = sqrt(rng());
		r = sqrt(1.0 - (z * z));
		phi = 2.0 * PI * rng();
		x = r * cos(phi);
		y = r * sin(phi);
		Vec u1, v1, w1;
		createLocalCoord(n, u1, v1, w1);
		u1 = u1 * x;
		v1 = v1 * y;
		w1 = w1 * z;
		i = u1 + v1 + w1;
		pdf = i.dot(n) / PI;
	}

	Vec kd;
};

//Ideal specular BRDF
struct SpecularBRDF : public BRDF {
	SpecularBRDF(Vec ks_) : ks(ks_) {}

	Vec eval(const Vec &n, const Vec &o, const Vec &i) const {
		Vec tempVec = mirroredDirection(n, o);
		double epsilon = 1e5;
		if (((abs(i.x - tempVec.x) < epsilon) && (abs(i.y - tempVec.y) < epsilon)) && (abs(i.z - tempVec.z) < epsilon)) {
			return ks * (1.0 / n.dot(i));
		}
		else {
			return 0.0;
		}

	}

	void sample(const Vec &n, const Vec &o, Vec &i, double &pdf) const {		//SAMPLE IMPLEMENTATION
		Vec wi = mirroredDirection(n,o);
		pdf = 1.0;
		i = wi;
	}

	Vec mirroredDirection(const Vec &n, const Vec &o) const {
		Vec temp = Vec();
		temp = (n * (2.0 * n.dot(o)) - o);
		return temp;
	}

	Vec ks;
};


/*
* Scene configuration
*/

// Pre-defined BRDFs
const DiffuseBRDF leftWall(Vec(.75, .25, .25)),
rightWall(Vec(.25, .25, .75)),
otherWall(Vec(.75, .75, .75)),
blackSurf(Vec(0.0, 0.0, 0.0)),
brightSurf(Vec(0.9, 0.9, 0.9));

//Pre-defined Specular BRDF
const SpecularBRDF specBRDF(Vec(0.999, 0.999, 0.999));

// Scene: list of spheres
const Sphere spheres[] = {
	Sphere(1e5,  Vec(1e5 + 1,40.8,81.6),   Vec(),         leftWall),   // Left
	Sphere(1e5,  Vec(-1e5 + 99,40.8,81.6), Vec(),         rightWall),  // Right
	Sphere(1e5,  Vec(50,40.8, 1e5),      Vec(),         otherWall),  // Back
	Sphere(1e5,  Vec(50, 1e5, 81.6),     Vec(),         otherWall),  // Bottom
	Sphere(1e5,  Vec(50,-1e5 + 81.6,81.6), Vec(),         otherWall),  // Top
	Sphere(16.5, Vec(27,16.5,47),        Vec(),         brightSurf), // Ball 1
	Sphere(16.5, Vec(73,16.5,78),        Vec(),         brightSurf), // Ball 2
	Sphere(5.0,  Vec(50,70.0,81.6),      Vec(50,50,50), blackSurf)   // Light
};

// Camera position & direction
const Ray cam(Vec(50, 52, 295.6), Vec(0, -0.042612, -1).normalize());


/*
* Global functions
*/

Vec radiance(const Ray &r, const Sphere &s, Vec xN, int depth);
Vec reflectedRadiance(const Ray &r, const Sphere &s, Vec xN, int depth);
Vec directRadiance(const Ray &r, const Sphere &s, const Sphere &lSource, Vec xN, int depth);
Vec indirectRadiance(const Ray &r, const Sphere &s, Vec xN, int depth);
void luminaireSample(const Sphere &s, Vec &i, Vec &ni, double &pdf);
float visible(const Ray &r, const Ray &n);

bool intersect(const Ray &r, double &t, int &id) {
	double n = sizeof(spheres) / sizeof(Sphere), d, inf = t = 1e20;
	for (int i = int(n); i--;) if ((d = spheres[i].intersect(r)) && d<t) { t = d; id = i; }
	return t<inf;
}


/*
* KEY FUNCTION: radiance estimator
*/

Vec receivedRadiance(const Ray &r, int depth, bool flag) {		// r has the current  position and the direction of the ray pointing to the previous x poistion
	double t;                                   // Distance to intersection
	int id = 0;                                 // id of intersected sphere

	if (!intersect(r, t, id)) return Vec();   // if miss, return black
	const Sphere &obj = spheres[id];            // the hit object

	Vec x = r.o + r.d*t;                        // The intersection point  //x1
	Vec o = (Vec() - r.d).normalize();          // The outgoing direction (= -r.d)  //wicurr

	Vec n = (x - obj.p).normalize();            // The normal direction	//n at x
	if (n.dot(o) < 0) n = n*-1.0;

	/*
	Tips

	1. Other useful quantities/variables:
	Vec Le = obj.e;                             // Emitted radiance
	const BRDF &brdf = obj.brdf;                // Surface BRDF at x

	2. Call brdf.sample() to sample an incoming direction and continue the recursion
	*/

	Vec rad;
	Ray outgoing(x, o);

	rad = radiance(outgoing, obj, n, depth);


	///////////////////////////////
	return rad;   // FIXME
}

//////////////////////////GENERAL RADIANCE FUNCTION
Vec radiance(const Ray &r, const Sphere &s, Vec xN, int depth) {
	Vec rad;
	rad = s.e + reflectedRadiance(r, s, xN, depth);		//reflectedRadiance requires the sphere that was intersected with to operate to call directradiance
	return rad;
}


/////////////////////////////REFLECTED RADIANCE FUNCTION
Vec reflectedRadiance(const Ray &r, const Sphere &s, Vec xN, int depth) {		// r has the current  position and the direction of the ray pointing to the previous x poistion
	Vec rad;			// rad is the reflected radiance at this point
	const Sphere &light = spheres[7];
	rad = directRadiance(r, s, light, xN, depth) + indirectRadiance(r, s, xN, depth);
	return rad;
}

/////////////////////////////DIRECT RADIANCE		//pass in the sphere to be used as the luminaired source (can access stuff like emitted radiance)
															//also pass in sphere that "r" originates from
Vec directRadiance(const Ray &r, const Sphere &s, const Sphere &lSource, Vec xN, int depth) {		
	Vec result = Vec();
	Vec y, yN, dirRad;
	double pdf, r2;
	luminaireSample(lSource, y, yN, pdf);
	dirRad = (y - r.o).normalize();
	Ray toLight(r.o, dirRad);
	Ray yNormal(y, yN);
	r2 = (y - r.o).dot((y - r.o));
	result = ((lSource.e).mult(s.brdf.eval(xN, r.d, dirRad))) * visible(toLight, yNormal) * xN.dot(dirRad) * yN.dot(Vec() - dirRad) * (1.0 / (r2 * pdf));
	return result;
}

////////////////////////////INDIRECT RADIANCE(FIX ME)	///must recursively call the reflected radiance function

Vec indirectRadiance(const Ray &r, const Sphere &s, Vec xN, int depth) {
	Vec incDir, Le, rad;
	int rrDepth = 5, id = 0;
	float survivalProbability = 0.9;
	float p, randVal;
	double t, probDF;                                   // Distance to intersection

	if (depth <= rrDepth) {
		p = 1.0;
	} else {
		p = survivalProbability;
	}
	randVal = rng();
	if (randVal < p) {
		s.brdf.sample(xN, r.o, incDir, probDF);
		Ray xOut(r.o, incDir);
		if (!intersect(xOut, t, id)) return Vec();   // if miss, return black
		const Sphere &obj = spheres[id];            // the hit object

		Vec x = xOut.o + xOut.d*t;                        // The intersection point  //x-1
		Vec o = (Vec() - xOut.d).normalize();          // The outgoing direction (= -r.d)  //wicurr

		Vec n = (x - obj.p).normalize();            // The normal direction	//n at x-1
		if (n.dot(o) < 0) n = n*-1.0;

		const BRDF &brdf = obj.brdf;                // Surface BRDF at x-1

		Ray outgoing(x, o);			//outgoing ray at position x (which is x-1)
		rad = (reflectedRadiance(outgoing, obj, n, (depth + 1))).mult(s.brdf.eval(xN, r.d, incDir)) * xN.dot(incDir) * (1.0 / (probDF * p));

	} else {
		rad = Vec();
	}

	return rad;
}

////////////LUMINAIRE SAMPLE FUNCTION

void luminaireSample(const Sphere &s, Vec &i, Vec &ni, double &pdf) {		//LUMINARE SAMPLE IMPLEMENTATION (returns a point "i", normal to "i" called "ni", and a pdf.
	float rand1, rand2, z, x, y;
	rand1 = rng();
	rand2 = rng();
	z = (2.0 * rand1) - 1.0;
	x = sqrt(1 - (pow(z, 2.0))) * cos(2.0 * PI * rand2);
	y = sqrt(1 - (pow(z, 2.0))) * sin(2.0 * PI * rand2);
	ni = Vec(x, y, z);
	i = s.p + (ni * s.rad);
	pdf = (1.0 / (4.0 * PI * pow(s.rad,2.0)));
}


// Visibility function

float visible(const Ray &r, const Ray &n) {
	double t;	// Distance to intersection
	int id = 0;									// id of intersected sphere
	if (!intersect(r, t, id)) {
		return 0.0;
	} else {
		if (id == 7) {
			Vec fromLight = (Vec() - r.d).normalize();
			if (fromLight.dot(n.d) > 0) {
				return 1.0;
			} else {
				return 0.0;
			}
		} else {
			return 0.0;
		}
	}
}


/*
* Main function (do not modify)
*/


int main(int argc, char *argv[]) {
	int nworkers = omp_get_num_procs();
	omp_set_num_threads(nworkers);
	rng.init(nworkers);

	int w = 480, h = 360, samps = argc == 2 ? atoi(argv[1]) / 4 : 1; // # samples
	Vec cx = Vec(w*.5135 / h), cy = (cx.cross(cam.d)).normalize()*.5135;
	std::vector<Vec> c(w*h);

#pragma omp parallel for schedule(dynamic, 1)
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			const int i = (h - y - 1)*w + x;

			for (int sy = 0; sy < 2; ++sy) {
				for (int sx = 0; sx < 2; ++sx) {
					Vec r;
					for (int s = 0; s<samps; s++) {
						double r1 = 2 * rng(), dx = r1<1 ? sqrt(r1) - 1 : 1 - sqrt(2 - r1);
						double r2 = 2 * rng(), dy = r2<1 ? sqrt(r2) - 1 : 1 - sqrt(2 - r2);
						Vec d = cx*(((sx + .5 + dx) / 2 + x) / w - .5) +
							cy*(((sy + .5 + dy) / 2 + y) / h - .5) + cam.d;
						r = r + receivedRadiance(Ray(cam.o, d.normalize()), 1, true)*(1. / samps);
					}
					c[i] = c[i] + Vec(clamp(r.x), clamp(r.y), clamp(r.z))*.25;
				}
			}
		}
#pragma omp critical
		fprintf(stderr, "\rRendering (%d spp) %6.2f%%", samps * 4, 100.*y / (h - 1));
	}
	fprintf(stderr, "\n");

	// Write resulting image to a PPM file
	FILE *f = fopen("image.ppm", "w");
	fprintf(f, "P3\n%d %d\n%d\n", w, h, 255);
	for (int i = 0; i<w*h; i++)
		fprintf(f, "%d %d %d ", toInt(c[i].x), toInt(c[i].y), toInt(c[i].z));
	fclose(f);

	return 0;
}