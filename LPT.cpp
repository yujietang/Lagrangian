#include "Inlet1D.h"
#include "cantera/IdealGasMix.h"
#include "cantera/transport.h"
#include "Sim1D.h"
#include "SprayStFlow.h"
#include "Lagrangian.h"

#include <fstream>

using namespace Cantera;
using fmt::print;

int main()
{
/********************Set Injection********************/
    doublereal parcelDiameter(50e-6); // m
    doublereal injTemperature(300); // K
    doublereal injPressure(1.0*OneAtm); // Pa
    size_t dropletNumber(20e+6);
    //for single component of ethanol fuel:
    doublereal C_atoms = 2.0; // C2H5OH
    doublereal H_atoms = 6.0; // C2H5OH
    doublereal O_atoms = 1.0; // C2H5OH

    doublereal minGrid = parcelDiameter;//min grid size.

    /********************Set Gas Flow********************/
    IdealGasMix gas("Ethanol_31.cti", "gas");

    doublereal temp = 300.0; // K
    doublereal pressure = 1.0*OneAtm; //atm
    doublereal uin = 0.1; //m/sec
    doublereal phi = 1.0; //equivalence ratio

    size_t nsp = gas.nSpecies();
    vector_fp x(nsp, 0.0);
    doublereal ax = C_atoms + H_atoms/4.0 - O_atoms/2.0; // air consumption
    doublereal fa_stoic = 1.0 / (4.76 * ax); // fuel / air ratio at stoic state
    x[gas.speciesIndex("C2H5OH")] = 1.0;
    x[gas.speciesIndex("O2")] = 0.21 / phi / fa_stoic;
    x[gas.speciesIndex("N2")] = 0.78 / phi / fa_stoic;
    x[gas.speciesIndex("AR")] = 0.01 / phi / fa_stoic;

    // x[gas.speciesIndex("O2")] = 0.21;
    // x[gas.speciesIndex("N2")] = 0.78;
    // x[gas.speciesIndex("AR")] = 0.01;
    gas.setState_TPX(temp, pressure, x.data());
    // gas.setState_TP(temp, pressure);

    doublereal rho_in = gas.density();
    std::cout << "inlet rhog = " << rho_in << std::endl;
    vector_fp yin(nsp);
    vector_fp yout(nsp);

    gas.getMassFractions(&yin[0]);
    gas.equilibrate("HP");//evaluate the adiabatic flame temperature.
    gas.getMassFractions(&yout[0]);

    doublereal rho_out = gas.density();
    doublereal Tad = gas.temperature();

    print("phi = {}, Tad = {}\n", phi, Tad);

    /**********Lagrangian Cloud**********/
    Lagrangian cloud(
        parcelDiameter,
        injTemperature,
        injPressure,
        dropletNumber
    );
    /**********Set liquid Fuel**********/
    // std::string fuel("C2H5OH");
    // std::vector<std::string> fuelName;
    // fuelName.push_back(fuel);
    // std::cout << "fuel number = " << fuelName.size() << std::endl;
    // cloud.setFuel(fuelName);
    /**********Eulerian Flow**********/
    StFlow gasflow(&gas);
    gasflow.setFreeFlow();
    gasflow.setupCloud(cloud);
    // create an initial grid
    int nz = 10;//initial grid point number
    doublereal lz = 0.1;//initial grid length
    vector_fp z(nz);//initial grid point vector
    doublereal dz = lz/((doublereal)(nz - 1));//initial grid size
    //initialize the grid:
    for (int iz = 0; iz < nz; ++iz){
        z[iz] = ((doublereal)iz)*dz;
    }

    gasflow.setupGrid(nz, &z[0]);

    // specify the objects to use to compute kinetic rates and
    // transport properties
    std::unique_ptr<Transport> trunityLe(newTransportMgr("UnityLewis", &gas));

    gasflow.setTransport(*trunityLe);
    gasflow.setKinetics(gas);
    gasflow.setPressure(pressure);

    /******Create the inlet******/
    Inlet1D inlet;
    inlet.setMoleFractions(x.data());
    doublereal mdot = uin*rho_in;
    inlet.setMdot(mdot);
    cloud.setMpdot(mdot);
    inlet.setTemperature(temp);
    /******Create the outlet******/
    Outlet1D outlet;
    /******Create the domain******/
    std::vector<Domain1D*> domains { &inlet, &gasflow, &outlet };
    gasflow.setupCloud(cloud);
    cloud.GasFlow(&gasflow);
    std::cout << "Initial grid point number = "
            << gasflow.grid().size() << std::endl;
    
    Sim1D sprayflame(domains);
    sprayflame.Cloud(cloud);
    /******Supply the initial guess******/
    vector_fp locs{0.0, 0.3, 0.7, 1.0};
    vector_fp value;
    double uout = inlet.mdot()/rho_out;//TODO: mdot at inlet can not fulfill the continuity
    value = {uin, uin, uout, uout};
    sprayflame.setInitialGuess("u", locs, value);
    value = {temp, temp, Tad, Tad};
    sprayflame.setInitialGuess("T", locs, value);
    
    for(int k = 0; k < nsp; ++k){
        value = {yin[k], yin[k], yout[k], yout[k]};
        sprayflame.setInitialGuess(gas.speciesName(k), locs, value);
    }

    inlet.setMoleFractions(x.data());
    inlet.setMdot(mdot);
    inlet.setTemperature(temp);

    sprayflame.showSolution();

    int flowdomain = 1;
    double ratio = 10.0;
    double slope = 0.08;
    double curve = 0.1;

    sprayflame.setRefineCriteria(flowdomain, ratio, slope, curve);

    int loglevel = 1;

    /**********Solve freely propagating flame with spray cloud**********/
    sprayflame.setFixedTemperature(0.5 * (temp + Tad));
    gasflow.solveEnergyEqn();
    bool refine_grid = false;
    sprayflame.solve(loglevel, refine_grid); 
    double flameSpeed_mix = sprayflame.value(flowdomain,gasflow.componentIndex("u"),0);
    print("Flame speed with mixture-averaged transport: {} m/s\n",
            flameSpeed_mix);

    const vector_fp& solution = sprayflame.solutionVector();

    vector_fp zvec,Tvec,COvec,CO2vec,Uvec;

    print("\n{:9s}\t{:8s}\t{:5s}\t{:7s}\n",
            "z (m)", "T (K)", "U (m/s)", "Y(CO)");
    for (size_t n = 0; n < gasflow.nPoints(); n++) {
        Tvec.push_back(sprayflame.value(flowdomain,gasflow.componentIndex("T"),n));
        COvec.push_back(sprayflame.value(flowdomain,gasflow.componentIndex("CO"),n));
        CO2vec.push_back(sprayflame.value(flowdomain,gasflow.componentIndex("CO2"),n));
        Uvec.push_back(sprayflame.value(flowdomain,gasflow.componentIndex("u"),n));
        zvec.push_back(gasflow.grid(n));
        print("{:9.6f}\t{:8.3f}\t{:5.3f}\t{:7.5f}\n",
                gasflow.grid(n), Tvec[n], Uvec[n], COvec[n]);
    }

    print("\nAdiabatic flame temperature from equilibrium is: {}\n", Tad);
    print("Flame speed for phi={} is {} m/s.\n", phi, Uvec[0]);

    std::ofstream outfile("flamespeed.csv", std::ios::trunc);
    outfile << "  Grid,   Temperature,   Uvec,   CO,    CO2\n";
    for (size_t n = 0; n < gasflow.nPoints(); n++) {
        print(outfile, " {:11.3e}, {:11.3e}, {:11.3e}, {:11.3e}, {:11.3e}\n",
                gasflow.grid(n), Tvec[n], Uvec[n], COvec[n], CO2vec[n]);
    }
    std::cout << "$$$$$$$$$$$$$$$$$$$$$$$$$" << std::endl;
    for(int i=0; i<solution.size(); ++i)
    {
        std::cout << "solution vector [" 
                    << i
                    << "] = "
                    << solution[i]
                    << std::endl;
    }

    /****************************************************************************/
    /********************Set Injection********************/
    doublereal parcelDiameter(10e-6); // m
    doublereal injTemperature(300); // K
    doublereal injPressure(1.0*OneAtm); // Pa
    size_t dropletNumber(1);
    //for single component of ethanol fuel:
    doublereal C_atoms = 2.0; // C2H5OH
    doublereal H_atoms = 6.0; // C2H5OH
    doublereal O_atoms = 1.0; // C2H5OH

    doublereal minGrid = parcelDiameter;//min grid size.

    /********************Set Gas Flow********************/
    IdealGasMix gas("Ethanol_31.cti", "gas");

    doublereal temp = 300.0; // K
    doublereal pressure = 1.0*OneAtm; //atm
    doublereal uin = 0.1; //m/sec
    doublereal phi = 1.0; //equivalence ratio

    size_t nsp = gas.nSpecies();
    vector_fp x(nsp, 0.0);
    doublereal ax = C_atoms + H_atoms/4.0 - O_atoms/2.0; // air consumption
    doublereal fa_stoic = 1.0 / (4.76 * ax); // fuel / air ratio at stoic state
    x[gas.speciesIndex("C2H5OH")] = 1.0;
    x[gas.speciesIndex("O2")] = 0.21 / phi / fa_stoic;
    x[gas.speciesIndex("N2")] = 0.78 / phi / fa_stoic;
    x[gas.speciesIndex("AR")] = 0.01 / phi / fa_stoic;

    // x[gas.speciesIndex("O2")] = 0.21;
    // x[gas.speciesIndex("N2")] = 0.78;
    // x[gas.speciesIndex("AR")] = 0.01;
    gas.setState_TPX(temp, pressure, x.data());
    // gas.setState_TP(temp, pressure);

    doublereal rho_in = gas.density();
    std::cout << "inlet rhog = " << rho_in << std::endl;
    vector_fp yin(nsp);
    vector_fp yout(nsp);

    gas.getMassFractions(&yin[0]);
    gas.equilibrate("HP");//evaluate the adiabatic flame temperature.
    gas.getMassFractions(&yout[0]);

    doublereal rho_out = gas.density();
    doublereal Tad = gas.temperature();

    print("phi = {}, Tad = {}\n", phi, Tad);

    /**********Lagrangian Cloud**********/
    Lagrangian cloud(
        parcelDiameter,
        injTemperature,
        injPressure,
        dropletNumber
    );
    /**********Set liquid Fuel**********/
    // std::string fuel("C2H5OH");
    // std::vector<std::string> fuelName;
    // fuelName.push_back(fuel);
    // std::cout << "fuel number = " << fuelName.size() << std::endl;
    // cloud.setFuel(fuelName);
    /**********Eulerian Flow**********/
    StFlow gasflow(&gas);
    gasflow.setFreeFlow();
    gasflow.setupCloud(cloud);
    // create an initial grid
    int nz = 10;//initial grid point number
    doublereal lz = 0.1;//initial grid length
    vector_fp z(nz);//initial grid point vector
    doublereal dz = lz/((doublereal)(nz - 1));//initial grid size
    //initialize the grid:
    for (int iz = 0; iz < nz; ++iz){
        z[iz] = ((doublereal)iz)*dz;
    }

    gasflow.setupGrid(nz, &z[0]);

    // specify the objects to use to compute kinetic rates and
    // transport properties
    std::unique_ptr<Transport> trunityLe(newTransportMgr("UnityLewis", &gas));

    gasflow.setTransport(*trunityLe);
    gasflow.setKinetics(gas);
    gasflow.setPressure(pressure);

    /******Create the inlet******/
    Inlet1D inlet;
    inlet.setMoleFractions(x.data());
    doublereal mdot = uin*rho_in;
    inlet.setMdot(mdot);
    cloud.setMpdot(mdot);
    inlet.setTemperature(temp);
    /******Create the outlet******/
    Outlet1D outlet;
    /******Create the domain******/
    std::vector<Domain1D*> domains { &inlet, &gasflow, &outlet };
    gasflow.setupCloud(cloud);
    cloud.GasFlow(&gasflow);
    std::cout << "Initial grid point number = "
            << gasflow.grid().size() << std::endl;

    //solution vector derived from initial gas flow field:

    cloud.clearGasFlow();
    cloud.evalGasFlow(solution);
    cloud.evalTransf();
    cloud.solve();
       
    return 0;
}
