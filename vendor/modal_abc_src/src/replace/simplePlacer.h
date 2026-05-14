#ifndef __SIMPLE_REPLACEmain__
#define __SIMPLE_REPLACEmain__

#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <iomanip>

#include "base/main/main.h"

class AbcGlobalPlacer {
private:
    Abc_Ntk_t * pNtk;
    
    // Placement parameters
    float alpha;           // Force scaling factor for density
    float step_size;       // Movement step size
    float chip_xMin;      // Chip dimensions
    float chip_xMax;
    float chip_yMin;      // Chip dimensions
    float chip_yMax;
    
    // Temporary storage for gradients
    std::vector<float> grad_x;
    std::vector<float> grad_y;
    
    // Helper function to calculate HPWL gradient for a specific object in a specific net
    std::pair<float, float> calculate_hpwl_gradient_in_net(Abc_Obj_t * pObj, 
                                                           float min_x, float max_x, 
                                                           float min_y, float max_y);
    
public:
    // Constructor
    AbcGlobalPlacer(Abc_Ntk_t * pNetwork);
    
    // Destructor
    ~AbcGlobalPlacer();
    
    // Initialize random placement for non-terminal nodes
    void random_initial_placement();
    
    // Update density map based on current object positions
    void update_density_map();
    
    // Calculate density gradient for an object based on bin density
    std::pair<float, float> calculate_density_gradient(Abc_Obj_t * pObj);

    // Calculate density gradient for an object based on pair-wise repulsive force
    std::pair<float, float> calculate_density_gradient_dist(Abc_Obj_t * pObj);
    
    // Calculate HPWL gradient for an object based on its connections
    std::pair<float, float> calculate_wirelength_gradient(Abc_Obj_t * pObj);
    
    // Calculate total wirelength (for monitoring)
    float calculate_total_wirelength();
    
    // Perform one iteration with pathology detection
    void force_directed_iteration(bool);
    
    // Main global placement algorithm with aggressive density control
    void run_global_placement(int, bool);
    
    // Helper function to get overflow
    float get_overflow();
    
    // Export placement results for visualization
    void export_placement_results(const char* filename);
};

#endif
