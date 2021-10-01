#include <array>

#include "Gamma/Filter.h"
#include "Gamma/Noise.h"
#include "Gamma/Oscillator.h"
#include "al/app/al_App.hpp"
#include "al/graphics/al_Shapes.hpp"
#include "al/math/al_Ray.hpp"
#include "al/sound/al_Reverb.hpp"
#include "al/ui/al_BoundingBox.hpp"
#include "al/ui/al_ControlGUI.hpp"
#include "al/ui/al_Parameter.hpp"
#include "al/ui/al_Pickable.hpp"

template <class T>  // The class is templated to allow a variety of data types
class SmoothValue {
 public:
  SmoothValue(float initialTime = 50.0f,
              std::string interpolationType =
                "log") {  // how much time, in ms, does it take to arrive (or approach target value)
    arrivalTime = initialTime;  // store the input value (default to 50ms)
    interpolation = interpolationType;
    calculateCoefficients();  // calculate a and b (it is a lowpass filter)
    z = 0.0f;
  }

  inline T process() {  // this function will be called per-sample on the data
    if (stepsTaken <= numSteps) {
      if (interpolation == "log") z = (targetValue * b) + (z * a);
      if (interpolation == "lin") z += linStep;
      stepsTaken += 1;
    }
    return z;  // return the new z value (the output sample)
  }

  void setTime(float newTime) {  // set time (in ms)
    arrivalTime = newTime;       // store the input value
    numSteps = arrivalTime * 0.001f * gam::sampleRate();
    if (interpolation == "log") calculateCoefficients();  // calculate a and b
  }

  void setTarget(T newTarget) {
    targetValue = newTarget;
    linStep = (targetValue - z) / numSteps;
    stepsTaken = 0;
  }
  void changeType(std::string newType) { interpolation = newType; }
  T getCurrentValue() { return z; }
  T getTargetValue() { return targetValue; }
  float getTime() { return arrivalTime; }

 private:
  void calculateCoefficients() {  // called only when 'setTime' is called (and in constructor)
    a = std::exp(-(M_PI * 2) / numSteps);  // rearranged lpf coeff calculations
    b = 1.0f - a;
  }

  T targetValue;      // what is the destination (of type T, determind by implementation)
  T currentValue;     // how close to the destination? (output value)
  float arrivalTime;  // how long to take
  float a, b;         // coefficients
  T linStep;          // size of step in linear interpolation
  T numSteps;         // number of steps in interpolation
  int stepsTaken;     // variable to keep track of steps to avoid overshooting
  T z;                // storage for previous value
  std::string interpolation;
};

struct Spring {
  Spring(const char *name) {
    bundle.name(name);
    bundle << k;
  }
  al::Parameter k{"Stiffness", "physics", 100.0f, "", 1.0f, 100.0f};
  al::ParameterBundle bundle;
};

struct Particle {
 public:
  Particle() {
    Lop.type(gam::FilterType(gam::LOW_PASS));
    Lop.set(400, 1);
    freq = 0;
    oscillator.freq(freq);
    bell.freq(freq);
    bellEnv = 0;
    amSmooth.setTime(20.0f);
    fmSmooth.setTime(20.0f);
    panSmooth.setTime(20.0f);
    graphMesh.primitive(al::Mesh::LINE_STRIP);
  }

  void setEquilibrium(al::Vec3d newEquilibrium) { equilibrium = newEquilibrium; }

  al::Vec3d getEquilibrium() { return equilibrium; }

  void setPos(al::Vec3d pos) { particle.pose.setPos(pos); }
  void setPos(double x, double y, double z) { particle.pose.setPos(al::Vec3d(x, y, z)); }
  double x() { return particle.pose.get().x(); }
  double y() { return particle.pose.get().y(); }
  double z() { return particle.pose.get().z(); }

  void resetPos(bool x, bool y, bool z) {
    al::Vec3d newPosition = particle.pose.get();
    if (x) newPosition.x = equilibrium.x;
    if (y) newPosition.y = equilibrium.y;
    if (z) newPosition.z = equilibrium.z;
    particle.pose.setPos(newPosition);
  }

  void x(double newX) {
    al::Vec3d position = {newX, particle.pose.get().y(), particle.pose.get().z()};
    particle.pose.setPos(position);
  }
  void y(double newY) {
    al::Vec3d position = {particle.pose.get().x(), newY, particle.pose.get().z()};
    particle.pose.setPos(position);
  }
  void z(double newZ) {
    al::Vec3d position = {particle.pose.get().x(), particle.pose.get().y(), newZ};
    particle.pose.setPos(position);
  }

  void updateDisplacement() {
    for (int i = 0; i < 3; i++) {
      prevDisplacement[i] = displacement[i];
      displacement[i] = particle.pose.get().pos()[i] - equilibrium[i];
    }
  }

  al::Vec3d getDisplacement() { return displacement; }

  void setNextPos(double step) {
    nextPosition =
      particle.pose.get().pos() + (velocity * step) + (acceleration * step * step * 0.5);
  }

  void addToNextAccel(al::Vec3d force) { nextAcceleration += force; }

  void setNextVelocity(float step) {
    nextVelocity = velocity + (acceleration + nextAcceleration) * (step * 0.5);
  }

  void update() {
    particle.pose.setPos(nextPosition);
    velocity = nextVelocity;
    acceleration = nextAcceleration;
    nextAcceleration = {0, 0, 0};
    updateDisplacement();
  }

  void resetVelocity(bool x, bool y, bool z) {
    if (x) velocity.x = 0;
    if (y) velocity.y = 0;
    if (z) velocity.z = 0;
  }

  void addVelocity(double x = 0, double y = 0, double z = 0) {
    velocity[0] += x;
    velocity[1] += y;
    velocity[2] += z;
  }
  void addVelocity(al::Vec3d newVelocity) { velocity += newVelocity; }

  // add acceleration to one axis
  // axis: 0 for x, 1 for y, 2 for z
  void addAcceleration(int axis, double val) { acceleration[axis] += val; }

  void calculateAcceleration(float m, float b) {
    nextAcceleration = (nextAcceleration / m) - (velocity * b);  // (kx / m) - vb
  }

  al::Vec3d getAcceleration() { return acceleration; }

  void setAcceleration(al::Vec3d newAcceleration) { acceleration = newAcceleration; }

  void resetAcceleration() { acceleration = {0, 0, 0}; }

  void setTuningRatio(float newRatio) { tuningRatio = newRatio; }

  float getTuningRatio() { return tuningRatio; }

  void setFreq(float newFreq) {
    freq = newFreq;
    bell.freq(freq);
    oscillator.freq(freq);
  }
  float getFreq() { return freq; }

  void setFMModFreq(float fmMultiplier) { FM.freq(freq * fmMultiplier); }

  void fmProcess(float fmWidth) {
    oscillator.freq(freq + (FM() * fmSmooth.process() * fmWidth * 1000));  // set freq
  }

  void setParticleMesh(al::Mesh &mesh) { particle.set(mesh); }

  void setParticleName(std::string name) {
    particleName = name;
    oscName["Xdisp"] = "/dispX/" + name;
    oscName["Ydisp"] = "/dispY/" + name;
    oscName["Zdisp"] = "/dispZ/" + name;
    oscName["Xpos"] = "/pan/" + name;
  }

  std::string getParticleName() { return particleName; }

  void checkZeroCrossing() {
    for (int i = 0; i < 3; i++) {  // check if zero crossing
      if (signbit(prevDisplacement[i]) != signbit(displacement[i]) &&
          abs(prevDisplacement[i] - displacement[i]) > 0.00001) {
        zeroTrigger[i] = true;
      }
    }
  }

  bool isZeroTrigger(int axis) {
    if (zeroTrigger[axis]) {
      zeroTrigger[axis] = 0;
      return 1;
    } else {
      return 0;
    }
  }

  void bellTrigger() { bellEnv = 1; }

  double bellProcess() {
    if (bellEnv > 0) bellEnv -= 0.00005;
    return bell() * Lop(this->bellEnv);
  }

  void draw(al::Graphics &g) { particle.drawMesh(g); }

  double processOscillator() { return oscillator(); }

  bool pickEvent(al::PickEvent e) { particle.event(e); }

  bool isSelected() { return particle.selected; }
  void clearSelection() { particle.clearSelection(); }

  al::Mesh graphMesh;  // mesh for drawing graph
  SmoothValue<float> amSmooth{20.0, "lin"};
  SmoothValue<float> fmSmooth{20.0, "lin"};
  SmoothValue<float> panSmooth{20.0, "lin"};
  std::map<std::string, std::string> oscName{
    {"Xdisp", ""}, {"Ydisp", ""}, {"Zdisp", ""}, {"Xpos", ""}};

  al::PickableBB particle;

 private:
  al::Vec3d velocity = {0, 0, 0};
  al::Vec3d acceleration = {0, 0, 0};
  al::Vec3d nextVelocity = {0, 0, 0};
  al::Vec3d nextAcceleration = {0, 0, 0};
  al::Vec3d nextPosition = {0, 0, 0};

  al::Vec3d equilibrium = {0, 0, 0};
  al::Vec3d displacement = {0, 0, 0};
  al::Vec3d prevDisplacement = {0, 0, 0};

  std::string particleName;

  gam::Sine<> oscillator;
  gam::Sine<> FM;
  gam::Sine<> bell;
  gam::Biquad<> Lop;
  float tuningRatio = 1;
  float freq;
  double bellEnv;
  bool zeroTrigger[3] = {0, 0, 0};
};

// to do, add 3rd dimension
struct ParticleNetwork {
 public:
  ParticleNetwork(unsigned int x = 4, unsigned int y = 1) {
    nX = x;
    nY = y;
    particles.clear();
    particles.resize(nX + 2);
    for (int y = 0; y <= nX + 1; y++) particles[y].resize(nY + 2);
  }

  Particle &operator()(int x, int y) { return particles[x][y]; }

  void resize(unsigned int newSizeX, unsigned int newSizeY) {
    nX = newSizeX;
    nY = newSizeY;
    particles.clear();
    particles.resize(nX + 2);
    for (int y = 0; y <= nX + 1; y++) particles[y].resize(nY + 2);

    xSprings.clear();
    ySprings.clear();

    springLength = 1.0f / (std::max(nX, nY) + 1);

    particleMesh.reset();
    addIcosphere(particleMesh, springLength / 5, 4);
    particleMesh.generateNormals();

    for (int x = 0; x <= nX + 1; x++) {
      for (int y = 0; y <= nY + 1; y++) {
        particles[x][y].setParticleMesh(particleMesh);
        particles[x][y].setEquilibrium(
          al::Vec3d((x * springLength) - ((springLength * (nX + 1)) / 2),
                    (y * springLength) - ((springLength * (nY + 1)) / 2), 0));
        particles[x][y].resetPos(1, 1, 1);
        particles[x][y].setParticleName(std::to_string(((y - 1) * nX) + x));
      }
    }
    retune(scale);
  }

  al::Vec3d applyForces(Particle particleOne, Particle particleTwo, float k) {
    al::Vec3d forceComponents = {0, 0, 0};  // Force Components
    if (!freedom[0] && !freedom[1] && !freedom[2]) {
      particleOne.resetVelocity(1, 1, 1);
      return forceComponents;  // return no force if no axis activated
    }

    al::Vec3d force = (particleTwo.getDisplacement() - particleOne.getDisplacement()) * k;

    if (freedom[0]) forceComponents[0] = force.x;
    if (freedom[1]) forceComponents[1] = force.y;
    if (freedom[2]) forceComponents[2] = force.z;
    return forceComponents;
  }

  // Update particle physics
  void update(double step) {
    int NX = particles.size();
    int NY = particles[0].size();

    for (int y = 1; y < NY - 1; y++)
      for (int x = 1; x < NX - 1; x++) particles[x][y].setNextPos(step);  // update position

    for (int y = 0; y < NY - 1; y++)
      for (int x = 0; x < NX - 1; x++) {
        al::Vec3d forcesX =
          applyForces(particles[x][y], particles[x + 1][y],
                      xSprings[x]->k);            // get forces between this spring and the next
        particles[x][y].addToNextAccel(forcesX);  // force due to right particle spring
        particles[x + 1][y].addToNextAccel(forcesX * -1);  // opposite force on right particle

        if (NY > 3) {  // only calculate Y forces if 2d array (NY > 1 + 2 boundary particles)
          al::Vec3d forcesY = applyForces(particles[x][y], particles[x][y + 1], ySprings[y]->k);
          particles[x][y].addToNextAccel(forcesY);           // force due to above particle spring
          particles[x][y + 1].addToNextAccel(forcesY * -1);  // opposite force on above particle
        }

        particles[x][y].calculateAcceleration(mass, damping);
      }

    for (int y = 1; y < NY - 1; y++)
      for (int x = 1; x < NX - 1; x++) particles[x][y].setNextVelocity(step);

    for (int y = 1; y < NY - 1; y++)
      for (int x = 1; x < NX - 1; x++)
        if (!particles[x][y].particle.selected) particles[x][y].update();
  }

  void retune(std::vector<float> newScale) {
    scale = newScale;
    int step = 0;
    int octave = 1;
    for (int x = 1; x <= nX; x++) {
      for (int y = 1; y <= nY; y++) {
        if (step == scale.size() - 1) {
          octave *= scale[scale.size() - 1];
          step = 0;
        }
        particles[x][y].setTuningRatio(scale[step] * octave);
        particles[x][y].setFreq(scale[step] * octave * tuningRoot);
        step++;
      }
    }
  }

  void retune() {
    for (int x = 1; x <= nX; x++) {
      for (int y = 1; y <= nY; y++) {
        particles[x][y].setFreq(particles[x][y].getTuningRatio() * tuningRoot);
      }
    }
  }

  void setTuningRoot(float newRoot) { tuningRoot = newRoot; }

  int sizeX() { return nX; }
  int sizeY() { return nY; }

  void setFreedom(bool x, bool y, bool z) { freedom = {x, y, z}; }

  void setMass(float newMass) { mass = newMass; }
  void setDamping(float newDamping) { damping = newDamping; }

  std::vector<std::vector<Particle>> particles;
  double springLength;    // Spacing between particles
  al::Mesh particleMesh;  // mesh for drawing particle
  std::vector<Spring *> xSprings;
  std::vector<Spring *> ySprings;

 private:
  float tuningRoot;
  std::vector<float> scale;
  int nX;
  int nY;
  std::array<bool, 3> freedom{0, 0, 0};
  float mass;
  float damping;
};

class RingBuffer {
 public:
  RingBuffer(unsigned maxSize) : mMaxSize(maxSize) {
    mBuffer.resize(mMaxSize);
    mTail = 0;
    mPrevSample = 0;
  }

  unsigned getMaxSize() const { return mMaxSize; }

  void resize(unsigned maxSize) {
    mMaxSize = maxSize;
    mBuffer.resize(mMaxSize);
  }

  void push_back(float value) {
    mMutex.lock();
    mTail = (mTail + 1) % mMaxSize;
    mBuffer[mTail] = value;
    mMutex.unlock();
  }

  int getTail() const { return mTail; }

  float at(int index) {
    if (index >= mMaxSize) {
      std::cerr << "RingBuffer index out of range." << std::endl;
      index = index % mMaxSize;
    }
    if (mMutex.try_lock()) {
      mPrevSample = mBuffer.at(index);
      mMutex.unlock();
    }
    return mPrevSample;
  }

  float operator[](unsigned index) { return this->at(index); }

  const float *data() { return mBuffer.data(); }

  float getRMS(unsigned lookBackLength) {
    int start = mTail - lookBackLength;
    if (start < 0) start = mMaxSize + start;

    float val = 0.0;
    for (unsigned i = 0; i < lookBackLength; i++) {
      val += pow(mBuffer[(start + i) % mMaxSize], 2);
    }
    return sqrt(val / lookBackLength);
  }

  void print() const {
    for (auto i = mBuffer.begin(); i != mBuffer.end(); ++i) std::cout << *i << " ";
    std::cout << "\n";
  }

 private:
  std::vector<float> mBuffer;
  unsigned mMaxSize;
  int mTail;
  float mPrevSample;

  std::mutex mMutex;
};

/// BundleGUIManager copied from Allolib and modified to give access to "global"
class ChonBundle {
  /// @ingroup UI
 public:
  ChonBundle(bool global) { mBundleGlobal = global; }

  void drawBundleGUI() {
    std::unique_lock<std::mutex> lk(mBundleLock);
    std::string suffix = "##_bundle_" + mName;
    al::ParameterGUI::drawBundleGroup(mBundles, suffix, mCurrentBundle, mBundleGlobal);
  }

  ChonBundle &registerParameterBundle(al::ParameterBundle &bundle) {
    if (mName.size() == 0 || bundle.name() == mName) {
      std::unique_lock<std::mutex> lk(mBundleLock);
      if (mName.size() == 0) {
        mName = bundle.name();
      }
      mBundles.push_back(&bundle);
    } else {
      std::cout << "Warning: bundle name mismatch. Bundle '" << bundle.name() << "' ingnored."
                << std::endl;
    }
    return *this;
  };

  /// Register parameter using the streaming operator.
  ChonBundle &operator<<(al::ParameterBundle &newBundle) {
    return registerParameterBundle(newBundle);
  }

  /// Register parameter using the streaming operator.
  ChonBundle &operator<<(al::ParameterBundle *newBundle) {
    return registerParameterBundle(*newBundle);
  }

  std::string name() { return mName; }

  int &currentBundle() { return mCurrentBundle; }
  bool &bundleGlobal() { return mBundleGlobal; }
  bool &bundleGlobal(bool global) { mBundleGlobal = global; }
  std::vector<al::ParameterBundle *> bundles() { return mBundles; }

 private:
  std::mutex mBundleLock;
  std::vector<al::ParameterBundle *> mBundles;
  std::string mName;
  int mCurrentBundle{0};
  bool mBundleGlobal{false};
};
