#ifndef TEDSTORE_KEYSERVER_HPP
#define TEDSTORE_KEYSERVER_HPP

#include "configure.hpp"
#include "cryptoPrimitive.hpp"
#include "dataStructure.hpp"
#include "messageQueue.hpp"
#include "openssl/bn.h"
#include "optimalSolver.hpp"
#include "ssl.hpp"
#include <bits/stdc++.h>

#define KEYMANGER_PRIVATE_KEY "key/server.key"
#define SECRET_SIZE 16

class keyServer {
private:
    CryptoPrimitive* cryptoObj_;
    u_int** sketchTable_;
    uint64_t sketchTableCounter_;
    double T_;
    bool opSolverFlag_;
    vector<pair<string, int>> opInput_;
    int opm_;
    uint64_t sketchTableWidith_;
    std::mutex multiThreadEditTMutex_;
    std::mutex multiThreadEditSketchTableMutex_;
    random_device rd_;
    mt19937_64 gen_;
    u_char keyServerPrivate_[SECRET_SIZE];
    int optimalSolverComputeItemNumberThreshold_;
    ssl* keySecurityChannel_;

public:
    keyServer(ssl* keySecurityChannelTemp);
    ~keyServer();
    void runKeyGen(SSL* connection);
    void runOptimalSolver();
};

#endif //TEDSTORE_KEYSERVER_HPP
