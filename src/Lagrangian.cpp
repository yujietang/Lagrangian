#include "Lagrangian.h"
#include "Liquid.h"
#include "AllSpecies.h"

#include <cmath>

namespace Cantera
{
Lagrangian::Lagrangian(
   IdealGasPhase* ph,
   const doublereal parcelDiameter,
   const doublereal TInjection,
   const doublereal pinjection,
   const doublereal injectionPosition,
   const doublereal phi_gas,
   const doublereal phi_over,
   const doublereal fa_st_mass,
   const size_t fuelIndex
) :
    Thermo(0),
    fuel(pinjection),
    d_inj(parcelDiameter),
    Tp_inj(TInjection),
    z_inj(injectionPosition),
    Phi_over(phi_over),
    Phi_gas(phi_gas),
    fa_st_m(fa_st_mass),
    kf(fuelIndex),
    p_inj(pinjection),
    Np(1),
    gas(0),
    small(1.0e-14)
{
    Thermo = ph;
    setupInjection(d_inj, 
                Tp_inj,
                Mdotp_inj);
    std::cout << "\n>>>>>>>>>>>>>> Parcel Initialization <<<<<<<<<<<<<" << std::endl;
    std::cout << "injection temperature = " << Tp_inj << std::endl;
    std::cout << "injection pressure = " << p_inj << std::endl;
    std::cout << "droplet mass = " << Md_inj << std::endl;
    std::cout << "parcel's diameter = " << d_inj << std::endl;
}

void Lagrangian::setupInjection(
    doublereal d_inj,
    doublereal Tp_inj,
    doublereal Mdotp_inj
)
{
    doublereal Rhop_inj = fuel.rho(Tp_inj);
    doublereal Vd_inj = Pi*std::pow(d_inj, 3.0)/6.0;
    Md_inj = Rhop_inj*Vd_inj;
}

void Lagrangian::evalGasFlow(const vector_fp& solution)
{
    size_t Nvar = c_offset_Y + gas->nsp();
    std::cout << "Number of variables at each grid point: "
            << Nvar << std::endl;
    //pointer to the gas-phase field:
    z = gas->grid();
    p0 = gas->pressure();
    rhog = gas->density();
    mug = gas->viscosity();
    kappag = gas->heatConductivity();
    cpg = gas->cp();
    mw = gas->MW();
    Yg.resize(gas->nsp());  
    for(size_t i = 0; i < solution.size(); ++i)
    {
        if(i%Nvar==0){
            ug.push_back(solution[i]);
        }
        else if(i%Nvar==2){
            Tg.push_back(solution[i]);
        }
        else
        {
            for(size_t k=0; k<gas->nsp(); ++k){
                if(i%Nvar==(k+c_offset_Y)){
                    Yg[k].push_back(solution[i]);
                }
            }
        }
    }
    //get the laminar flame speed
    Sl = ug[0];
    rho_inlet = rhog[0];
    umax = *max_element(ug.begin(),ug.end());
    std::cout << "\nthe max velocity is\t" << umax << std::endl;
    std::cout << "\nthe spray flame speed is\t" << Sl << "\n" << std::endl;
    // for(int k=0; k<Tg.size();++k){
    //     std::cout <<"@ k" << k << "\t" << Tg[k]<<"\t\t" << rhog[k] 
    //             <<"\t\t" << ug[k]  <<std::endl;
    // }
}

void Lagrangian::inject(doublereal injectionPosition)
{
    double xp_inj = injectionPosition - z[0];
    double up_inj = intpfield(ug, z, (injectionPosition - z[0]));
    xp.push_back(xp_inj);
    mp.push_back(Md_inj);
    Tp.push_back(Tp_inj);
    up.push_back(ug[0]);
    dp.push_back(d_inj);
    rhop.push_back(fuel.rho(Tp_inj));

    mtfd_.push_back(0.0);
    htfd_.push_back(0.0);
    mtfp_.push_back(0.0);
    htfp_.push_back(0.0);
    for(size_t k=0;k<gas->nsp();++k){
        stfp_[k].push_back(0.0);
    }
    

    Np = 1;
    dtlag = Co*(z[1]-z[0])/umax;
    Nd = dtlag*(rho_inlet*Sl/(1+fa_st_m*Phi_gas)*(Phi_over-Phi_gas)*fa_st_m/Md_inj);
    std::cout << "\n>>>>>>>>>>>>>>>>>>>> injection velocity = " << up_inj << std::endl;
    std::cout << "\n>>>>>>>>>>>>>>>>>>>> Nd = " << Nd << std::endl;
    std::cout << "\n>>>>>>>>>>>>>>>>>>>> dtlag = " << dtlag <<"\n"<< std::endl;
}

void Lagrangian::evalParcelFlow()
{
    rho_.resize(xp.size());
    mu_.resize(xp.size());
    u_.resize(xp.size());
    T_.resize(xp.size());
    cp_.resize(xp.size());
    kappa_.resize(xp.size());
    Y_.resize(gas->nsp());
    for(size_t k = 0; k < Y_.size(); ++k)
    {
        Y_[k].resize(xp.size());
    }

    //interpolate the gas flow field into the parcel's position:
    for(size_t ip = 0; ip < xp.size(); ++ip )
    {
        rho_[ip] = intpfield(rhog, z, xp[ip]);
        mu_[ip] = intpfield(mug, z, xp[ip]);
        u_[ip] = intpfield(ug, z, xp[ip]);
        T_[ip] = intpfield(Tg, z, xp[ip]);
        cp_[ip] = intpfield(cpg, z, xp[ip]);
        kappa_[ip] = intpfield(kappag, z, xp[ip]);
        for(size_t k=0; k<Y_.size(); ++k){
            Y_[k][ip] = intpfield(Yg[k], z, xp[ip]);
        }
    }
}

void Lagrangian::solve()
{
    clearParcel();
    this->inject(z_inj);

    doublereal time = 0;
    doublereal _xp = (z_inj - z[0]);
    doublereal _ug = ug[0];
    doublereal _Tp; 
    doublereal _rhog = rhog[0];
    doublereal _mug = mug[0];
    doublereal _Red;

    size_t marchingStep = 2e+5;    //tracking step n:

    for(size_t n=1; n<marchingStep; ++n)
    {
        if((dp.back()< small) || (mp.back()< small) || (_xp > gas->zmax()))
        {
            break;
        }

        if(up[0]<0.01)
        {
            std::cerr << "Extinction happens!!!\n\n";
            exit(0);
        }

        _xp += up[n-1]*dtlag;

        //parcel's position:
        xp.push_back(_xp);
        // std::cout << "Tracking time = " << time + (n-1)*dtlag << ",\t"
        //          << "Parcel's position = " << xp.back() << "\n" << std::endl;
        
        evalParcelFlow();
        
        //parcel's mass:
        mp.push_back(
            mp[n-1] + dtlag*mddot(n-1)
        );

        //parcel's temperature:
        _Tp = Tp[n-1] + dtlag*Tddot(n-1);
        Tp.push_back(
            (_Tp < fuel.Tb() ? _Tp : fuel.Tb())
        );

        //parcel's density
        rhop.push_back(
            fuel.rho(Tp[n])
        ); 

        //parcel's diameter 
        dp.push_back(
            pow(6*mp[n]/(Pi*rhop[n]+small), 0.33333)
        );

        // //parcel's velocity
        // //Yuan & Chen (1976):
        // std::cout<<"LOCAL Parcel VELOCITY AT x_p = "<< xp[n-1] <<" = "<< up[n-1] <<"\n"<<std::endl;
        _ug = intpfield(ug, z, xp[n-1]);
        _rhog = intpfield(rhog, z, xp[n-1]);
        _mug = intpfield(mug, z, xp[n-1]);
        _Red = _rhog*std::abs(_ug - up[n-1])*dp[n-1]/(_mug+small);
        up.push_back(
            up[n-1] + dtlag*0.75*Cd(_Red)*_rhog*std::abs(_ug-up[n-1])*(_ug-up[n-1])/(dp[n-1]*rhop[n-1]+small)
            // _ug //no drag force
        ); 
        
        /******For droplet******/
        //mass transfer:
        mtfd_.push_back(
            mddot(n)
        );
        //heat transfer:
        htfd_.push_back(
            Tddot(n)
        );
        /******For parcel******/
        //mass transfer:
        mtfp_.push_back(
            mtfp(n)
        );
        //heat transfer:
        htfp_.push_back(
            htfp(n)
        );

        for(size_t k=0; k<gas->nsp(); ++k){
            stfp_[k].push_back(
                stfp(k,n)
            );
        }

        ++Np;

        if(Np != mp.size()){
            std::cerr << "\nError: parcel's number is not equal to Np!\n";
            exit(0);
        }
    }
}

void Lagrangian::recordOldValue()
{
    for(size_t ii = 0; ii < mtf_.size(); ++ii){
        mtfOld_[ii] = mtf(ii);
        htfOld_[ii] = htf(ii);
        for(size_t k=0;k<gas->nsp();++k){
            stfOld_[k][ii] = stf(k, ii);
        }
    }
}

void Lagrangian::evalTransf()
{
    this->recordOldValue();
    //resize the vector of quantities transfer:
    htf_.resize(z.size(), 0.0);
    mtf_.resize(z.size(), 0.0);//TODO: multi-component case is a 2d vector.
    stf_.resize(gas->nsp());
    for(size_t k=0;k<gas->nsp();++k){
        stf_[k].resize(z.size(), 0.0);
    }
    doublereal leftz;
    doublereal rightz;
    doublereal leftLength;
    doublereal rightLength;
    doublereal dz;
    
    for(size_t iz = 0; iz < z.size()-1; ++iz)
    {
        dz = z[iz+1] - z[iz] + small;
        leftz = z[iz];
        rightz = z[iz+1];
        for(size_t ip = 0; ip < Np; ++ip)
        {
            if(xp[ip] >= leftz && xp[ip] <= rightz){
                leftLength = xp[ip] - z[iz];
                rightLength = z[iz+1] - xp[ip];
                mtf_[iz] += mtfp_[ip]*(rightLength/dz);
                mtf_[iz+1] += mtfp_[ip]*(leftLength/dz);
                htf_[iz] += htfp_[ip]*(rightLength/dz); 
                htf_[iz+1] += htfp_[ip]*(leftLength/dz);
                for(size_t k=0;k<gas->nsp();++k){
                    if(k==kf){
                        stf_[k][iz] += (stfp_[k][ip]+Yg[k][iz]*mtfp_[ip])*(rightLength/dz);
                        stf_[k][iz+1] += (stfp_[k][ip]+Yg[k][iz+1])*(leftLength/dz);
                    }else{
                        stf_[k][iz] += Yg[k][iz]*stfp_[k][ip]*(rightLength/dz);
                        stf_[k][iz+1] += Yg[k][iz+1]*stfp_[k][ip]*(leftLength/dz);
                    }
                }
            }
            else{
                mtf_[iz] += 0.0;
                mtf_[iz+1] += 0.0;
                htf_[iz] += 0.0;
                htf_[iz+1] += 0.0;
                for(size_t k=0;k<gas->nsp();++k){
                    stf_[k][iz] += 0.0;
                    stf_[k][iz+1] += 0.0;
                }
            }
        }
    }
    // std::cout << "\t\tmtf_ \t\thtf_" << std::endl;
    // for(size_t ii=0;ii<mtf_.size();++ii){
    //     std::cout << "@" << ii << "\t" << mtf_[ii] << "\t\t\t\t" << htf_[ii] << std::endl;
    // }
}

void Lagrangian::clearParcel()
{
    //clear parcel:
    dp.clear();
    mp.clear();
    up.clear();
    Tp.clear();
    xp.clear();

    mtfd_.clear();
    htfd_.clear();
    mtfp_.clear();
    htfp_.clear();
    stfp_.resize(gas->nsp());
    for(size_t k=0;k<gas->nsp();++k){
        stfp_[k].clear();
    }
    std::cout << "====== Parcels has been cleared up! ======\n"<< std::endl;
}

void Lagrangian::clearGasFlow(bool do_spray)
{   
    if(do_spray == true){
        //record species fraction:
        Ygtmp.resize(gas->nsp());
        for(int k=0;k<Ygtmp.size();++k){
            for(int j=0;j<Yg[k].size();++j){
                Ygtmp[k].resize(Yg.size(),0.0);
            }
        }
        for(int k=0;k<Ygtmp.size();++k){
            for(int j=0;j<Yg[k].size();++j){
                Ygtmp[k][j] = Yg[k][j];
            }
        }
        //clear gas flow:
        z.clear();
        ug.clear();
        mug.clear();
        rhog.clear();
        Tg.clear();
        cpg.clear();
        kappag.clear();
        mw.clear();
        Yg.resize(gas->nsp());
        for(size_t k=0; k<Yg.size(); ++k){
            Yg[k].clear();
        }            
    }
    else{
        mtf_.resize(gas->grid().size(), 0.0);
        htf_.resize(gas->grid().size(), 0.0);
        stf_.resize(gas->nsp());
        for(size_t k=0; k<gas->nsp();++k){
            stf_[k].resize(gas->grid().size(), 0.0);
        }
        
        mtfOld_.resize(gas->grid().size(), 0.0);
        htfOld_.resize(gas->grid().size(), 0.0);
        stfOld_.resize(gas->nsp());
        Ygtmp.resize(gas->nsp());
        for(size_t k=0; k<gas->nsp();++k){
            stfOld_[k].resize(gas->grid().size(), 0.0);
            Ygtmp[k].resize(gas->grid().size(), 0.0);
        }
    }
}

doublereal Lagrangian::intpfield(vector_fp& field, vector_fp& grid, doublereal xp) const
{
    if(xp < gas->zmin()){
        std::cerr << "ERROR: Parcel does not reach the inlet!" << std::endl;
        return 0;
    }
    else if(xp > gas->zmax()){
        return 0;
    }
    else if(xp == gas->zmin()){
        return field[0];
    }
    else if(xp == gas->zmax()){
        return field.back();
    }
    else
    {
        size_t j_L = lower_bound(z.begin(), z.end(), xp) - z.begin() - 1;
        size_t j_R = lower_bound(z.begin(), z.end(), xp) - z.begin();

        doublereal z_L = z[j_L];
        doublereal z_R = z[j_R];

        return field[j_R]*(xp - z_L)/(z_R - z_L) 
            + field[j_L]*(z_R - xp)/(z_R - z_L); 
    }
}

// bool Lagrangian::evalRsd(const size_t& Nloop, const vector_fp& solution)
// {      
//     if(Nloop == 1)
//     {
//         std::cout << "Starting the Two-Way Coupled evaluation!" << std::endl;
//         Told = Tg.back();
//         return false;
//     }
//     else{
//         Tnew = Tg.back();
//         doublereal rsd = 1.0;
//         std::cout << "\nCheck the Two-Way Coupled residual ...\n" << std::endl;
//         std::cout << "\n Told = " << Told << std::endl;
//         std::cout << "\n Tnew = " << Tnew << std::endl;
//         /*****evaluate the residual*****/
//         rsd = abs(Tnew - Told)/(Told + small);
//         std::cout << "\nThe coupled RSD = " << rsd << std::endl;
//         if(rsd < 2e-3){
//             std::cout << "\nResidual Checking has been done!\n" << std::endl;
//             return true;
//         }
//         else{
//             Told = Tnew;
//             return false;
//         }
//     }
// }

bool Lagrangian::evalResidual(const size_t& Nloop, const bool ifAddSpraySource, const vector_fp& solution)
{
    if(ifAddSpraySource){
        TNew_.resize(Tg.size());
        for(size_t ii=0; ii<Tg.size(); ++ii){
            TNew_[ii] = Tg[ii];
        }
        double sum = 0.0;
        double rsd = 1.0;
        for(size_t ii=0; ii<TOld_.size(); ++ii){
            sum += (abs(TNew_[ii] - TOld_[ii])/TOld_[ii]);
        }
        rsd = sum/(TOld_.size()+small);
        if(rsd < 1e-4){
            std::cout << "\nTwo way couple step "<< Nloop << "\t success!"<< std::endl;
            std::cout << "Residual =\t" << rsd << "\n" <<std::endl;
            std::cout << "the flame temperature is \t" << Tg.back() << "\n" << std::endl;
            return true;
        }else{
            for(size_t ii=0; ii<TOld_.size(); ++ii){
                std::cout << "\nTwo way couple step "<< Nloop << "\t failure! \n"<< std::endl;
                std::cout << "Residual =\t" << rsd << "\n" <<std::endl;
                TOld_[ii] = TNew_[ii];
                std::cout << "the flame temperature is \t" << Tg.back() << "\n" << std::endl;
                return false;
            }
        }
    }
    else{
        TOld_.resize(Tg.size());
        for(size_t ii=0; ii<Tg.size(); ++ii){
            TOld_[ii] = Tg[ii];
        }
        return false;
        // return true;
    }

}

void Lagrangian::setMpdot(doublereal mdot)
{
    Mdotp_inj = mdot;
}

doublereal Lagrangian::mddot(size_t n) //[kg/s]
{
    const doublereal MWf = mw[kf];//TODO: multi-component fuel
    //all parameters needed:
    doublereal Ni;
    doublereal kc;
    doublereal Cs;
    doublereal Cinf;
    doublereal psat;
    doublereal Sh;
    doublereal Red;
    doublereal Sc;
    doublereal rp;
    doublereal Dab;
    
    doublereal pc = p0;
    doublereal Tinf = T_[n];
    doublereal uG = u_[n];
    doublereal muG = mu_[n];
    doublereal rhoG = rho_[n];
    vector_fp Yc(Y_.size(), 0.0);
    for(size_t k=0; k<Yc.size(); ++k){
        Yc[k] = Y_[k][n];
    }

    //calculate the surface (vapour film) values:
    doublereal Ts, rhos, mus, Pr, kappas;
    Ts = (2*Tp[n] + Tinf)/3;
    
    const doublereal TRatio = Tinf/Ts;
    rhos = rhoG*TRatio;
    mus = muG/TRatio;

    // saturation pressure for species i [pa]
    psat = fuel.pv(Tp[n]);

    //vapour diffusivity [m2/s]:
    Dab = fuel.D(Ts);
 
    //Schmidt number:
    Sc = mus/(rhos*Dab + small);

    //droplet Reynold's number:
    Red = rhos*std::abs(uG - up[n])*dp[n]/mus;
    
    //Ranz-Marshall:
    Sh = 2 + 0.522*pow(Red, 2.0)*pow(Sc, 0.33333);

    //species volume fraction in the carrier gas:
    //the fuel index is k = 30:
    vector_fp Xc_(Yc.size(), 0.0);
    for(size_t k=0; k<Yc.size(); ++k){
        Xc_[k] = Yc[k]/mw[k];
    }
    double Xc = 0;
    for(size_t k=0; k<Xc_.size(); ++k){
        Xc += Xc_[k];
    }
    Xc = Xc_[kf]/Xc;

    //vapor concentration at surface [kmol/m3] at film temperature:
    Cs = psat/(RR*Ts);

    //vapor concentration in bulk gas [kmol/m3] at film temperature:
    Cinf = Xc*pc/(RR*Ts);

    //mass transfer coefficient [m/s]:
    kc = Sh*Dab/(dp[n] + small);

    //molar flux of vapour [kmol/m2/s]:
    Ni = std::max(kc*(Cs - Cinf), 0.0);
    
    //Return the mass transfer [kg/s]:
    doublereal massTranfRate = -Pi*Ni*dp[n]*dp[n]*MWf;

    return massTranfRate;
}

/***********************Theoretical model of droplet evaporation************************/
doublereal Lagrangian::mddot_th(size_t n)
{
    const doublereal MWf = mw[kf];//TODO:only for ethanol
    //all parameters needed:
    doublereal Ni;
    doublereal kc;
    doublereal Cs;
    doublereal Cinf;
    doublereal psat;
    doublereal Sh;
    doublereal Red;
    doublereal Sc;
    doublereal rp;
    doublereal D;   
    doublereal pc = p0;
    doublereal Tinf = T_[n];
    doublereal Td = Tp[n];
    doublereal uG = u_[n];
    doublereal muG = mu_[n];
    doublereal rhoG = rho_[n];
    vector_fp Yc(Y_.size(), 0.0);
    vector_fp YkMW(Y_.size(), 0.0);
    vector_fp Xc(Yc.size(), 0.0);
    for(size_t k=0; k<Yc.size(); ++k){
        Yc[k] = Y_[k][n];
        YkMW[k] = Yc[k]/mw[k];
    }
    double sumYkMW = std::accumulate(YkMW.begin(), YkMW.end(), 0.0);
    for(size_t k=0; k<Yc.size(); ++k){
        Xc[k] = (Yc[k]/mw[k])/(sumYkMW);
    }
    //calculate the surface (vapour film) values:
    doublereal Ts, rhos, mus, Pr, kappas;
    Ts = (2*Td + Tinf)/3;
    // saturation pressure for species i [pa]
    psat = fuel.pv(Td);
    //vapour diffusivity [m2/s]:
    // doublereal Dab = fuel.D(Ts);
    doublereal Dab = 10.2e-6;
    //liquid surface fuel molar fraction:
    // Clausius-Clapeyron relation:
    doublereal Tboiling = fuel.Tb();
    // doublereal Xsf = (psat/p0)*exp((38.56/(RR))*((1.0/Tboiling)-(1.0/Td)));
    doublereal Xsf = (psat/p0)*exp((4640.16)*((1.0/Tboiling)-(1.0/Td)));
    doublereal Ysf = Xsf*mw[kf];
    const double TRatio = Tinf/Ts; 
    rhos = rhoG*TRatio;
    mus = muG/TRatio;
    //Schmidt number:
    Sc = mus/(rhos*Dab + small);
    //droplet Reynold's number:
    Red = rhos*std::abs(uG - up[n])*dp[n]/mus; 
    //Ranz-Marshall:
    Sh = 2 + 0.522*pow(Red, 2.0)*pow(Sc, 0.33333);  
    // Xsf = (psat/p0);
    doublereal Bm = (Xsf - Xc[kf])/(1 - Xsf);
    //Return the mass transfer [kg/s]:
    doublereal massTranfRate = -Pi*dp[n]*Sh*Dab*rhoG*log(1 + Bm);
    return massTranfRate;
}

doublereal Lagrangian::Tddot(size_t n) //[K/s]
{
    //For droplet:
    const doublereal MWf = mw[kf];//TODO:only for ethanol
    doublereal md = mp[n];
    doublereal Td = Tp[n];
    //For carrier phase:
    doublereal pc = p0;
    doublereal cpG = cp_[n];
    doublereal Tinf = T_[n];
    doublereal rhoG = rho_[n];
    doublereal uG = u_[n];
    doublereal muG = mu_[n];
    doublereal kG = kappa_[n];
    vector_fp Yc(Y_.size(), 0.0);
    vector_fp YkMW(Y_.size(), 0.0);
    vector_fp Xc(Yc.size(), 0.0);
    for(size_t k=0; k<Yc.size(); ++k){
        Yc[k] = Y_[k][n];
        YkMW[k] = Yc[k]/mw[k];
    }
    double sumYkMW = std::accumulate(YkMW.begin(), YkMW.end(), 0.0);
    for(size_t k=0; k<Yc.size(); ++k){
        Xc[k] = (Yc[k]/mw[k])/(sumYkMW);
    }

    //surface temperature:
    doublereal Ts = (2*Td + Tinf)/3.0;
    const double TRatio = Tinf/Ts; 
    // saturation pressure for species i [pa]
    doublereal psat = fuel.pv(Ts);

    //liquid surface fuel molar fraction:
    // Clausius-Clapeyron relation:
    doublereal Tboiling = fuel.Tb();
    // doublereal Xsf = (psat/p0)*exp((38.56/(RR))*((1.0/Tboiling)-(1.0/Td)));
    doublereal Xsf = (psat/p0);
    doublereal Ysf = Xsf*mw[kf];

    const double sqrtW = sqrt(mw[kf]);
    const double cbrtW = cbrt(mw[kf]);

    doublereal kappas = Ysf*cbrtW*kG;

    doublereal cps = Xsf*cpG;

    // double sumYiSqrtW = Ysf*sqrtW;
    double sumYiCbrtW = Ysf*cbrtW;

    cps = std::max(cps, small);

    kappas /= sumYiCbrtW;
    kappas *= TRatio;
    kappas = std::max(kappas, small);

    doublereal rhos = rhoG*TRatio;

    doublereal mus = muG/TRatio;
    
    doublereal Pr = cps*mus/kappas;

    //mass transfer rate:
    doublereal mddot_ = mddot(n);

    doublereal Red = rhos*std::abs(uG - up[n])*dp[n]/mus;
    
    doublereal Nu = 2.0 + 0.6*std::pow(Red, 0.5)*std::pow(Pr, 0.33333);

    doublereal tddot = (mddot_/(md*fuel.cp(Td)))*fuel.Lv(Td) + (Pi*dp[n]*Nu*kappas/(md*fuel.cp(Td)))*(Tinf-Td);

    return tddot;
}

doublereal Lagrangian::hTransRate(size_t n) //[J/m3*s]
{
    //For droplet:
    const doublereal MWf = mw[kf];//TODO:only for ethanol
    const vector_fp& hf_RT = Thermo->enthalpy_RT_ref();//
    doublereal md = mp[n];
    doublereal Td = Tp[n];
    //For carrier phase:
    doublereal pc = p0;
    doublereal cpG = cp_[n];
    doublereal Tinf = T_[n];
    doublereal rhoG = rho_[n];
    doublereal uG = u_[n];
    doublereal muG = mu_[n];
    doublereal kG = kappa_[n];
    vector_fp Yc(Y_.size(), 0.0);
    vector_fp YkMW(Y_.size(), 0.0);
    vector_fp Xc(Yc.size(), 0.0);
    for(size_t k=0; k<Yc.size(); ++k){
        Yc[k] = Y_[k][n];
        YkMW[k] = Yc[k]/mw[k];
    }
    double sumYkMW = std::accumulate(YkMW.begin(), YkMW.end(), 0.0);
    for(size_t k=0; k<Yc.size(); ++k){
        Xc[k] = (Yc[k]/mw[k])/(sumYkMW);
    }

    //surface temperature:
    doublereal Ts = (2*Td + Tinf)/3.0;
    const double TRatio = Tinf/Ts; 
    // saturation pressure for species i [pa]
    doublereal psat = fuel.pv(Ts);

    //liquid surface fuel molar fraction:
    // Clausius-Clapeyron relation:
    doublereal Tboiling = fuel.Tb();
    // doublereal Xsf = (psat/p0)*exp((38.56/(RR))*((1.0/Tboiling)-(1.0/Td)));
    doublereal Xsf = (psat/p0);
    doublereal Ysf = Xsf*mw[kf];

    const double sqrtW = sqrt(mw[kf]);
    const double cbrtW = cbrt(mw[kf]);

    doublereal kappas = Ysf*cbrtW*kG;

    doublereal cps = Xsf*cpG;

    // double sumYiSqrtW = Ysf*sqrtW;
    double sumYiCbrtW = Ysf*cbrtW;

    cps = std::max(cps, small);

    kappas /= sumYiCbrtW;
    kappas *= TRatio;
    kappas = std::max(kappas, small);

    doublereal rhos = rhoG*TRatio;

    doublereal mus = muG/TRatio;
    
    doublereal Pr = cps*mus/kappas;

    //mass transfer rate:
    doublereal mddot_ = mddot(n);

    doublereal Red = rhos*std::abs(uG - up[n])*dp[n]/mus;
    
    doublereal Nu = 2.0 + 0.6*std::pow(Red, 0.5)*std::pow(Pr, 0.33333);

    doublereal tddot = (mddot_/(md*fuel.cp(Td)))*fuel.Lv(Td) 
                        + (Pi*dp[n]*Nu*kappas/(md*fuel.cp(Td)))*(Tinf-Td);

    doublereal hTransfdot = md*fuel.cp(Td)*tddot;
    
    //heat transfer rate from gas to liquid(convection heat transfer):
    doublereal qd = hTransfdot - mddot_*fuel.Lv(Td);

    doublereal Tls = ((Ts>fuel.Tb()) ? fuel.Tb() : Ts);
    // std::cout <<"\n"<< n <<"\t"<< qd << "\t"
    //         << qd - mddot_*fuel.cpg(0.5*(Tinf+Tls))*(Tinf-Tls) << "\t"
    //         << std::endl;
    return qd - mddot_*fuel.cpg(0.5*(Tinf+Tls))*(Tinf-Tls);
    // return qd + mddot_*fuel.Lv(Td); 
}

void Lagrangian::write() const
{
    double t = 0;
    std::ofstream fout1("./result/2way_d50.csv");
    fout1 << "# t [s], xp [m], d [micron], d^2 [micron^2], mp [mg], Tp [K], Tg [K], ug [m/s], Sm [kg/m3s], Sh [J/m3s]" << std::endl;
    for(size_t ip = 0; ip < xp.size(); ++ip){
        if((ip%1) == 0){
            fout1 << (t+ip*dtlag) << ","
                << xp[ip] << ","
                << dp[ip]*(1e+6) << ","
                << std::pow(dp[ip]*(1e+6), 2.0) << ","
                << mp[ip]*(1e+6) << ","
                << Tp[ip] << ","
                << T_[ip] << ","
                << u_[ip] << ","
                << mtfp_[ip] << ","
                << htfp_[ip] << std::endl;
        }
    }
}
}
