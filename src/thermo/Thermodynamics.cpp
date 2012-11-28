#include "Thermodynamics.h"
#include "utilities.h"
#include "XMLite.h"
#include "Constants.h"
#include "GfcEquilSolver.h"
#include "AutoRegistration.h"
#include "ThermoDB.h"
#include "StateModel.h"

#include <set>

using namespace std;
using namespace Numerics;

Thermodynamics::Thermodynamics(
    const vector<string> &species_names, const string& thermo_db,
    const string& state_model )
    : mp_work1(NULL), mp_work2(NULL), mp_default_composition(NULL),
      m_has_electrons(false)
{
    // Load the species and element objects for the specified species
    loadSpeciesFromList(species_names);
    
    // Now we can load the relevant thermodynamic database
    mp_thermodb = Factory<ThermoDB>::create(thermo_db, m_species);
    
    // Build the composition matrix
    m_element_matrix = RealMatrix(nSpecies(), nElements());
    
    for (int i = 0; i < nSpecies(); ++i)
        for (int j = 0; j < nElements(); ++j)
            m_element_matrix(i,j) = 
                m_species[i].nAtoms(m_elements[j].name());
    
    // Store the species molecular weights for faster access
    m_species_mw = RealVector(nSpecies());
    for (int i = 0; i < nSpecies(); ++i)
        m_species_mw(i) = m_species[i].molecularWeight();
    
    // Allocate storage for the work array
    mp_work1 = new double [nSpecies()];
    mp_work2 = new double [nSpecies()];
    
    // Default composition (every element has equal parts)
    mp_default_composition = new double [nElements()];
    std::fill(
        mp_default_composition, mp_default_composition + nElements(), 
        1.0 / nElements());
    
    // Allocate a new equilibrium solver
    mp_equil = new GfcEquilSolver(*this);
    
    // Allocate a new state model
    mp_state = Factory<StateModel>::create(state_model, nSpecies());
}

Thermodynamics::~Thermodynamics()
{
    delete [] mp_work1;
    delete [] mp_work2;
    delete [] mp_default_composition;
    
    delete mp_thermodb;
    delete mp_equil;
}

void Thermodynamics::loadSpeciesFromList(
    const std::vector<std::string> &species_names)
{
    // Determine file paths
    string thermo_directory = 
        utils::getEnvironmentVariable("MPP_DATA_DIRECTORY") + "/thermo";
    string elements_path    = thermo_directory + "/elements.xml";
    string species_path     = thermo_directory + "/species.xml";
    
    // First we need to load the entire element database for use in constructing
    // our species list
    XmlDocument element_doc(elements_path);
    
    XmlElement::Iterator element_iter = element_doc.root().begin();
    XmlElement::Iterator element_end  = element_doc.root().end();
    
    vector<Element> elements;
    
    for ( ; element_iter != element_end; ++element_iter)
        elements.push_back(Element(*element_iter));
    
    // Load the species XML database
    XmlDocument species_doc(species_path);    
    string species_name;
    
    // Use a set for the species names to ensure that species are listed only
    // once and so that the find() function can be used
    set<string> species_set(species_names.begin(), species_names.end());
    set<int>    used_elements;
    
    // Iterate over all species in the database and pull out the ones that are
    // needed from the list
    XmlElement::Iterator species_iter = species_doc.root().begin();
    XmlElement::Iterator species_end  = species_doc.root().end();
    
    for ( ; species_iter != species_end; ++species_iter) {
        // Get the name of current species
        species_iter->getAttribute("name", species_name);
        
        // Do we need this one?
        if (species_set.find(species_name) == species_set.end())
            continue;
        
        // We do, so store it
        m_species.push_back(Species(*species_iter, elements, used_elements));
        
        // Make sure that the electron (if it exists) is stored at the beginning
        // to make life easier.
        if (species_name == "e-") {
            if (m_species.size() > 1) 
                std::swap(m_species[0], m_species.back());
            m_has_electrons = true;
        }
        
        // Clear this species from the list of species that we still need to
        // find
        species_set.erase(species_name);
        
        // Break out early when they are all loaded
        if (species_set.size() == 0)
            break;
    }
    
    // Make sure all species were loaded (todo: better error message with file
    // name and list of missing species...)
    if (species_set.size() > 0) {
        cout << "Could not find all the species in listed in the mixture!";
        cout << endl << "Missing species:" << endl;
        
        set<string>::const_iterator missing_iter = species_set.begin();
        set<string>::const_iterator missing_end  = species_set.end();
        
        for ( ; missing_iter != missing_end; ++missing_iter)
            cout << setw(10) << *missing_iter << endl;
        
        exit(1);
    }
    
    
    // Now the species are loaded and the corresponding elements are determined
    // so store only the necessary elements in the class (note the ordering of
    // elements in the database is preserved here because of set)
    set<int>::const_iterator iter = used_elements.begin();
    set<int>::const_iterator end  = used_elements.end();
    
    for ( ; iter != end; ++iter)
        m_elements.push_back(elements[*iter]);
    
    // Finally store the species and element order information for easy access
    for (int i = 0; i < m_elements.size(); ++i)
        m_element_indices[m_elements[i].name()] = i;
    
    for (int i = 0; i < m_species.size(); ++i)
        m_species_indices[m_species[i].name()] = i;
}


void Thermodynamics::setDefaultComposition(
        const std::vector<std::pair<std::string, double> >& composition)
{
    // Make sure all elements are included exactly once and 
    bool set_element [nElements()];
    std::fill(set_element, set_element+nElements(), false);
    
    vector< pair<string, double> >::const_iterator iter = 
        composition.begin();
    
    for ( ; iter != composition.end(); ++iter) {
        int index = elementIndex(iter->first);
        if (index >= 0) {
            if (set_element[index]) {
                cerr << "Error: trying to set the default elemental"
                     << " composition for element " << iter->first
                     << " more than once!" << endl;
                exit(1);
            } else {
                mp_default_composition[index] = iter->second;
                set_element[index] = true;
            }
        } else {
            cerr << "Error: trying to set the default elemental"
                 << " composition for element " << iter->first
                 << " which is not in this mixture!" << endl;
            exit(1);
        }
    }
    
    for (int i = 0; i < nElements(); ++i) {
        if (!set_element[i]) {
            cerr << "Error: did not include element " << elementName(i)
                 << " while setting the default elemental compsotion"
                 << " of the mixture!" << endl;
            exit(1);
        }
    }
    
    // Scale the fractions to sum to one
    RealVecWrapper wrapper(mp_default_composition, nElements());
    wrapper = wrapper / wrapper.sum();
}

void Thermodynamics::setStateTPX(
    const double* const T, const double* const P, const double* const X)
{
    mp_state->setStateTPX(T, P, X);
}

void Thermodynamics::setStateTPY(
        const double* const T, const double* const P, const double* const Y)
{
    convert<Y_TO_X>(Y, mp_work1);
    mp_state->setStateTPX(T, P, mp_work1);
}

double Thermodynamics::T() const {
    return mp_state->T();
}

double Thermodynamics::Tr() const {
    return mp_state->Tr();
}

double Thermodynamics::Tv() const {
    return mp_state->Tv();
}

double Thermodynamics::Te() const {
    return mp_state->Te();
}

double Thermodynamics::Tel() const {
    return mp_state->Tel();
}

double Thermodynamics::P() const {
    return mp_state->P();
}

const double* const Thermodynamics::X() const {
    return mp_state->X();
}

double Thermodynamics::standardStateT() const {
    return mp_thermodb->standardTemperature();
}

double Thermodynamics::standardStateP() const {
    return mp_thermodb->standardPressure();
}

double Thermodynamics::mixtureMw() const {
    return (Numerics::asVector(mp_state->X(), nSpecies()) * 
        m_species_mw).sum();
}

void Thermodynamics::equilibrate(
    double T, double P, const double* const p_c, double* const p_X, 
    bool set_state) const
{
    //mp_state->setStateTPX(T, P, p_X);
    mp_equil->equilibrate(T, P, p_c, p_X);
    convert<CONC_TO_X>(p_X, p_X);
    if (set_state) mp_state->setStateTPX(T, P, p_X);
}

void Thermodynamics::equilibrate(double T, double P) const
{
    //for (int i = 0; i < nElements(); ++i)
    //    cout << elementName(i) << " " << mp_default_composition[i] << endl;
    equilibrate(T, P, mp_default_composition, mp_work1);
}

double Thermodynamics::numberDensity(const double T, const double P) const
{
    return P / (KB * T);
}

double Thermodynamics::numberDensity() const {
    double Xe = (hasElectrons() ? mp_state->X()[1] : 0.0);
    return mp_state->P() / KB * 
        ((1.0 - Xe) / mp_state->T() + Xe / mp_state->Te());
}

double Thermodynamics::pressure(
    const double T, const double rho, const double *const Y) const
{
    double pressure = 0.0;
    for (int i = 0; i < nSpecies(); ++i)
        pressure += Y[i] / speciesMw(i);
    pressure *= rho * T * RU;
    return pressure;
}
    
double Thermodynamics::density(
    const double T, const double P, const double *const X) const
{
    double density = 0.0;
    for (int i = 0; i < nSpecies(); ++i)
        density += X[i] * speciesMw(i);
    density *= P / (RU * T);
    return density;
}

double Thermodynamics::density() const
{
    return numberDensity() * mixtureMw() / NA;
}

void Thermodynamics::speciesCpOverR(double *const p_cp) const
{
    mp_thermodb->cp(
        mp_state->T(), mp_state->Te(), mp_state->Tr(), mp_state->Tv(),
        mp_state->Tel(), p_cp, NULL, NULL, NULL, NULL);
}

double Thermodynamics::mixtureFrozenCpMole() const
{
    double cp = 0.0;
    speciesCpOverR(mp_work1);
    for (int i = 0; i < nSpecies(); ++i)
        cp += mp_work1[i] * X()[i];
    return (cp * RU);
}

double Thermodynamics::mixtureFrozenCpMass() const {
    return mixtureFrozenCpMole() / mixtureMw();
}

double Thermodynamics::mixtureEquilibriumCpMole(
    double T, double P, const double* const Xeq) const
{
    const double eps = 1.0e-6;

    // Compute the current elemental fraction
    elementFractions(Xeq, mp_work1);
    
    // Compute equilibrium mole fractions at a perturbed temperature
    equilibrate(T*(1.0+eps), P, mp_work1, mp_work2, false);
    
    // Compute the species H/RT
    speciesHOverRT(mp_work1);
    
    // Update the frozen Cp with the dX_i/dT * h_i term
    double cp = 0.0;
    for (int i = 0; i < nSpecies(); i++)
        cp += (mp_work2[i] - Xeq[i]) * mp_work1[i];
    
    return cp / eps * RU + mixtureFrozenCpMole();
}

double Thermodynamics::mixtureEquilibriumCpMole() const
{
    return mixtureEquilibriumCpMole(T(), P(), X());
}

double Thermodynamics::mixtureEquilibriumCpMass() const
{
    return mixtureEquilibriumCpMole() / mixtureMw();
}   

double Thermodynamics::mixtureEquilibriumCpMass(
    double T, double P, const double* const X) const 
{    
    return mixtureEquilibriumCpMole(T, P, X) / mixtureMwMole(X);
}

double Thermodynamics::mixtureFrozenCvMole() const
{
    return mixtureFrozenCpMole() - RU;
}

double Thermodynamics::mixtureFrozenCvMass() const 
{
    return (mixtureFrozenCpMole() - RU) / mixtureMw();
}

double Thermodynamics::mixtureEquilibriumCvMole(
    double T, double P, const double* const X) const
{
    return mixtureEquilibriumCpMole(T, P, X) - RU;
}

double Thermodynamics::mixtureEquilibriumCvMole() const
{
    return mixtureEquilibriumCpMole() - RU;
}

double Thermodynamics::mixtureEquilibriumCvMass(
    double T, double P, const double* const X) const 
{
    return (mixtureEquilibriumCpMole(T, P, X) - RU) / mixtureMwMole(X);
}

double Thermodynamics::mixtureEquilibriumCvMass() const
{
    return (mixtureEquilibriumCpMole() - RU) / mixtureMw();
}
    
double Thermodynamics::mixtureFrozenGamma() const 
{
    double cp = mixtureFrozenCpMole();
    return (cp / (cp - RU));
}

double Thermodynamics::mixtureEquilibriumGamma(
    double T, double P, const double* const X) const 
{
    double cp = mixtureEquilibriumCpMole(T, P, X);
    return (cp / (cp - RU));
}

double Thermodynamics::mixtureEquilibriumGamma() const
{
    double cp = mixtureEquilibriumCpMole();
    return (cp / (cp - RU));
}

void Thermodynamics::speciesHOverRT(
    double* const h, double* const ht, double* const hr, double* const hv,
    double* const hel, double* const hf) const
{
    mp_thermodb->enthalpy(
        mp_state->T(), mp_state->Te(), mp_state->Tr(), mp_state->Tv(),
        mp_state->Tel(), h, ht, hr, hv, hel, hf);
}

double Thermodynamics::mixtureHMole() const
{
    double h = 0.0;
    speciesHOverRT(mp_work1);
    for (int i = 0; i < nSpecies(); ++i)
        h += mp_work1[i] * X()[i];
    return (h * RU * mp_state->T());
}

double Thermodynamics::mixtureHMass() const 
{
    return mixtureHMole() / mixtureMw();
}

void Thermodynamics::speciesSOverR(double *const p_s) const
{
    mp_thermodb->entropy(
        mp_state->T(), mp_state->Te(), mp_state->Tr(), mp_state->Tv(),
        mp_state->Tel(), mp_state->P(), p_s, NULL, NULL, NULL, NULL);
}

double Thermodynamics::mixtureSMole() const 
{
    double s = 0;
    speciesSOverR(mp_work1);
    for (int i = 0; i < nSpecies(); ++i)
        s += mp_work1[i] * X()[i];
    return (s * RU);
}

double Thermodynamics::mixtureSMass() const {
    return mixtureSMole() / mixtureMw();
}

void Thermodynamics::speciesGOverRT(double* const p_g) const
{
    mp_thermodb->gibbs(
        mp_state->T(), mp_state->Te(), mp_state->Tr(), mp_state->Tv(),
        mp_state->Tel(), mp_state->P(), p_g, NULL, NULL, NULL, NULL);
}

void Thermodynamics::speciesGOverRT(double T, double P, double* const p_g) const
{
    mp_thermodb->gibbs(T, T, T, T, T, P, p_g, NULL, NULL, NULL, NULL);
}

void Thermodynamics::elementMoles(
    const double *const species_N, double *const element_N) const
{
    asVector(element_N, nElements()) = 
        asVector(species_N, nSpecies()) * m_element_matrix;
}

void Thermodynamics::elementFractions(
    const double* const Xs, double* const Xe) const
{
    RealVecWrapper wrapper(Xe, nElements());
    wrapper = asVector(Xs, nSpecies()) * m_element_matrix;
    double sum = wrapper.sum();
    for (int i = 0; i < nElements(); ++i)
        Xe[i] /= sum;
}