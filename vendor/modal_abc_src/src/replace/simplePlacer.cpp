#include "simplePlacer.h"

// Constructor
AbcGlobalPlacer::AbcGlobalPlacer(Abc_Ntk_t * pNetwork) 
    : pNtk(pNetwork), alpha(10.0), step_size(0.3) {
    
    chip_xMin = pNtk->binOriX;
    chip_xMax = pNtk->binOriX + pNtk->binStepX * pNtk->binDimX;
    chip_yMin = pNtk->binOriY;
    chip_yMax = pNtk->binOriY + pNtk->binStepY * pNtk->binDimY;
    
    int nObjs = Abc_NtkObjNum(pNtk);
    grad_x.resize(nObjs, 0.0);
    grad_y.resize(nObjs, 0.0);

    // just test!
    // Abc_Obj_t * pObj, * pFanin, * pFanout;
    // int i, j;

    // Abc_NtkForEachObj(pNtk, pObj, i) {
    //     std::cout << "Id = " << pObj->Id << ' ' << pObj->Type << std::endl;
    //     if (Abc_ObjIsNode(pObj))
    //     {
    //         Abc_ObjForEachFanin( pObj, pFanin, j) {
    //             std::cout << "Id = " << pFanin->Id << ' ' << pFanin->Type << ' ';
    //         }
    //         std::cout << std::endl;
    //         Abc_ObjForEachFanout( pObj, pFanout, j) {
    //             std::cout << "Id = " << pFanout->Id << ' ' << pFanout->Type << ' ';
    //         }
    //         std::cout << std::endl;
    //     }
    // }
    // std::cout << "Ntk Type: " << pNtk->ntkType << std::endl;
    // Abc_NtkForEachNet(pNtk, pObj, i) {
    //     std::cout << "Id = " << pObj->Id << ' ' << pObj->Type << std::endl;
    //     {
    //         Abc_ObjForEachFanin( pObj, pFanin, j) {
    //             std::cout << "Id = " << pFanin->Id << ' ' << pFanin->Type << ' ';
    //         }
    //         std::cout << std::endl;
    //         Abc_ObjForEachFanout( pObj, pFanout, j) {
    //             std::cout << "Id = " << pFanout->Id << ' ' << pFanout->Type << ' ';
    //         }
    //         std::cout << std::endl;
    //     }
    // }


    // // Initialize bin parameters if not set
    // if (pNtk->binDimX == 0 || pNtk->binDimY == 0) {
    //     pNtk->binDimX = std::max(10, (int)(width / 100.0));
    //     pNtk->binDimY = std::max(10, (int)(height / 100.0));
    //     pNtk->binStepX = width / pNtk->binDimX;
    //     pNtk->binStepY = height / pNtk->binDimY;
    //     pNtk->binOriX = 0.0;
    //     pNtk->binOriY = 0.0;
    //     pNtk->nBins = pNtk->binDimX * pNtk->binDimY;
        
    //     // Allocate density array if not already allocated
    //     if (pNtk->binsDensity == nullptr) {
    //         pNtk->binsDensity = new float[pNtk->nBins];
    //         memset(pNtk->binsDensity, 0, pNtk->nBins * sizeof(float));
    //     }
        
    //     pNtk->invBinArea = 1.0 / (pNtk->binStepX * pNtk->binStepY);
    // }
    
    std::cout << "ABC Global Placer initialized:\n";
    std::cout << "  Network objects: " << nObjs << std::endl;
    std::cout << "  Chip size x: " << chip_xMin << "<= x <=" << chip_xMax << std::endl;
    std::cout << "  Chip size y: " << chip_yMin << "<= y <=" << chip_yMax << std::endl;
    std::cout << "  Bin grid: " << pNtk->binDimX << " x " << pNtk->binDimY << std::endl;
}

// Destructor
AbcGlobalPlacer::~AbcGlobalPlacer() {
    // Note: Don't delete pNtk->binsDensity here as it's managed by ABC
}

// Initialize random placement for non-terminal nodes
void AbcGlobalPlacer::random_initial_placement() {
    Abc_Obj_t * pNode;
    int i;
    
    srand(12345); // Fixed seed for reproducibility
    int placed_count = 0;
    Abc_NtkForEachNode(pNtk, pNode, i) {
            // Random placement within chip bounds, considering object size 
            pNode->xPos = pNtk->binOriX + pNode->half_den_sizeX + (rand() / (float)RAND_MAX) * ((float)pNtk->binDimX * pNtk->binStepX - 2 * pNode->half_den_sizeX);
            pNode->yPos = pNtk->binOriY + pNode->half_den_sizeY + (rand() / (float)RAND_MAX) * ((float)pNtk->binDimY * pNtk->binStepY - 2 * pNode->half_den_sizeY);
            placed_count++;
    }
    
    std::cout << "Randomly placed " << placed_count << " non-terminal nodes." << std::endl;
}

// Update density map based on current object positions
void AbcGlobalPlacer::update_density_map() {
    // Clear density map
    memset(pNtk->binsDensity, 0, pNtk->nBins * sizeof(float));
    
    float xMin, yMin, xMax, yMax;
    int xb0, xb1, yb0, yb1;
    float x0, x1, y0, y1;
    float xShare[10], yShare[10];
    Abc_Obj_t * pNode;
    int i;

    Abc_NtkForEachObj(pNtk, pNode, i) {
        if (!Abc_ObjIsNode(pNode))
            continue;
        xMin = pNode->xPos - pNode->half_den_sizeX;
        yMin = pNode->yPos - pNode->half_den_sizeY;
        xMax = pNode->xPos + pNode->half_den_sizeX;
        yMax = pNode->yPos + pNode->half_den_sizeY;
        xb0 = (int)((xMin - pNtk->binOriX) / pNtk->binStepX);
        yb0 = (int)((yMin - pNtk->binOriY) / pNtk->binStepY);
        xb1 = (int)((xMax - pNtk->binOriX) / pNtk->binStepX);
        yb1 = (int)((yMax - pNtk->binOriY) / pNtk->binStepY);
        if (xb0 < 0)
            xb0 = 0;
        if (yb0 < 0)
            yb0 = 0;
        if (xb1 >= pNtk->binDimX)
            xb1 = pNtk->binDimX - 1;
        if (yb1 >= pNtk->binDimY)
            yb1 = pNtk->binDimY - 1;
        // std::cout << xb0 << ' ' << xb1 << ' ' << xMin << ' ' << xMax << ' ' << pNode->Id << std::endl;
        assert (xb1 - xb0 >= 0 && xb1 - xb0 < 10);
        assert (yb1 - yb0 >= 0 && yb1 - yb0 < 10);
        
        for( int xIdx = xb0; xIdx <= xb1; xIdx++) {
            x0 = xIdx * pNtk->binStepX + pNtk->binOriX;
            x0 = std::max(x0, xMin);
            x1 = (xIdx + 1) * pNtk->binStepX + pNtk->binOriX;
            x1 = std::min(x1, xMax);
            xShare[xIdx - xb0] = x1 - x0;
        }

        for( int yIdx = yb0; yIdx <= yb1; yIdx++) {
            y0 = yIdx * pNtk->binStepY + pNtk->binOriY;
            y0 = std::max(y0, yMin);
            y1 = (yIdx + 1) * pNtk->binStepY + pNtk->binOriY;
            y1 = std::min(y1, yMax);
            yShare[yIdx - yb0] = y1 - y0;
        }

        for(int xIdx = xb0; xIdx <= xb1; xIdx++) {
            for(int yIdx = yb0; yIdx <= yb1; yIdx++) {
                pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] += xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNode->den_scal;
            }
        }
    }
}

// Calculate density gradient for an object based on bin density
std::pair<float, float> AbcGlobalPlacer::calculate_density_gradient(Abc_Obj_t * pObj) {
    assert(Abc_ObjIsNode(pObj));
    int bin_xMin = (int)((pObj->xPos - pObj->half_den_sizeX - pNtk->binOriX) / pNtk->binStepX);
    int bin_yMin = (int)((pObj->yPos - pObj->half_den_sizeY - pNtk->binOriY) / pNtk->binStepY);
    int bin_xMax = (int)((pObj->xPos + pObj->half_den_sizeX - pNtk->binOriX) / pNtk->binStepX);
    int bin_yMax = (int)((pObj->yPos + pObj->half_den_sizeY - pNtk->binOriY) / pNtk->binStepY);
    
    float grad_x = 0.0, grad_y = 0.0;

    int bottom_idx, top_idx;
    int left_idx, right_idx;
    float overflow_bottom, overflow_top, overflow_left, overflow_right, overflow_avg = 0.0;

    float x0, x1, y0, y1;
    float xShare[10], yShare[10];

    // assert( bin_xMin >= 0 && bin_xMax <= pNtk->binDimX - 1 && bin_xMax > bin_xMin);
    // assert( bin_yMin >= 0 && bin_yMax <= pNtk->binDimY - 1 && bin_yMax > bin_yMin);
    assert (bin_xMax - bin_xMin >= 0 && bin_xMax - bin_xMin < 10);
    assert (bin_yMax - bin_yMin >= 0 && bin_yMax - bin_yMin < 10);
    if ( bin_xMax == pNtk->binDimX )
        bin_xMax--;
    if ( bin_yMax == pNtk->binDimY )
        bin_yMax--;
    
    // Calculate average overflow
    for( int xIdx = bin_xMin; xIdx <= bin_xMax; xIdx++) {
        x0 = xIdx * pNtk->binStepX + pNtk->binOriX;
        x0 = std::max(x0, pObj->xPos - pObj->half_den_sizeX);
        x1 = (xIdx + 1) * pNtk->binStepX + pNtk->binOriX;
        x1 = std::min(x1, pObj->xPos + pObj->half_den_sizeX);
        xShare[xIdx - bin_xMin] = x1 - x0;
    }

    for( int yIdx = bin_yMin; yIdx <= bin_yMax; yIdx++) {
        y0 = yIdx * pNtk->binStepY + pNtk->binOriY;
        y0 = std::max(y0, pObj->yPos - pObj->half_den_sizeY);
        y1 = (yIdx + 1) * pNtk->binStepY + pNtk->binOriY;
        y1 = std::min(y1, pObj->yPos + pObj->half_den_sizeY);
        yShare[yIdx - bin_yMin] = y1 - y0;
    }

    for(int xIdx = bin_xMin; xIdx <= bin_xMax; xIdx++) {
        for(int yIdx = bin_yMin; yIdx <= bin_yMax; yIdx++) {
            overflow_avg += std::max(0.0, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] - 1.0) * xShare[xIdx - bin_xMin] * yShare[yIdx - bin_yMin];
        }
    }
    overflow_avg /= 4 * pObj->half_den_sizeX * pObj->half_den_sizeY;

    // Calculate gradient using central difference in neighboring bins
    for (int i = bin_yMin; i <= bin_yMax; ++i) {
        bottom_idx = bin_xMin * pNtk->binDimY + i;
        top_idx = bin_xMax * pNtk->binDimY + i;
        overflow_bottom = std::max(0.0, pNtk->binsDensity[bottom_idx] - 1.0);
        overflow_top = std::max(0.0, pNtk->binsDensity[top_idx] - 1.0);

        grad_x += (overflow_top - overflow_bottom + (pNtk->binsDensity[top_idx] - pNtk->binsDensity[bottom_idx]) * overflow_avg) * pObj->den_scal;
    }

    for (int i = bin_xMin; i <= bin_xMax; ++i) {
        left_idx = i * pNtk->binDimY + bin_yMin;
        right_idx = i * pNtk->binDimY + bin_yMax;
        overflow_left = std::max(0.0, pNtk->binsDensity[left_idx] - 1.0);
        overflow_right = std::max(0.0, pNtk->binsDensity[right_idx] - 1.0);

        grad_y += (overflow_right - overflow_left + (pNtk->binsDensity[right_idx] - pNtk->binsDensity[left_idx]) * overflow_avg) * pObj->den_scal;
    }

    return {grad_x, grad_y};
}

// Calculate density gradient for an object based on pair-wise repulsive force
std::pair<float, float> AbcGlobalPlacer::calculate_density_gradient_dist(Abc_Obj_t * pObj) {
    assert(Abc_ObjIsNode(pObj));
    float grad_x = 0.0, grad_y = 0.0;
    float xDist, yDist, dist;
    float force;
    Abc_Obj_t * pObjTmp;
    int i;
    int counter = 0;
    Abc_NtkForEachObj(pNtk, pObjTmp, i) {
        if (!Abc_ObjIsNode(pObjTmp) || pObjTmp->Id == pObj->Id || pObjTmp->Id == 0)
            continue;
        xDist = pObj->xPos - pObjTmp->xPos;
        yDist = pObj->yPos - pObjTmp->yPos;
        if (xDist == 0 && yDist == 0)
        {
            xDist += (rand() / (float)RAND_MAX > 0.5) ? 0.01 : -0.01;
            yDist += (rand() / (float)RAND_MAX > 0.5) ? 0.01 : -0.01;
        }
        dist = sqrt(xDist * xDist + yDist * yDist);
        force = 0.1 / (dist * dist);
        // grad_x += -(xDist / dist * force * fmax((pObj->half_den_sizeX > pNtk->binStepX * 1.414) ? pObj->half_den_sizeX : pObj->half_den_sizeX / (pNtk->binStepX * 1.414) + (pObjTmp->half_den_sizeX > pNtk->binStepX * 1.414) ? pObjTmp->half_den_sizeX : pObjTmp->half_den_sizeX / (pNtk->binStepX * 1.414) - abs(xDist), 0));
        // grad_y += -(yDist / dist * force * fmax((pObj->half_den_sizeY > pNtk->binStepY * 1.414) ? pObj->half_den_sizeY : pObj->half_den_sizeY / (pNtk->binStepY * 1.414) + (pObjTmp->half_den_sizeY > pNtk->binStepY * 1.414) ? pObjTmp->half_den_sizeY : pObjTmp->half_den_sizeY / (pNtk->binStepY * 1.414) - abs(xDist), 0));
        grad_x += -(xDist / dist * force * fmax(pObj->half_den_sizeX + pObjTmp->half_den_sizeX - abs(xDist), 0));
        grad_y += -(yDist / dist * force * fmax(pObj->half_den_sizeY + pObjTmp->half_den_sizeY - abs(xDist), 0));
        counter++;
    }
    // grad_x /= (float)counter;
    // grad_y /= (float)counter;
    // std::cout << grad_x << ' ' << grad_y << std::endl;

    return {grad_x, grad_y};
}

// Calculate HPWL gradient for an object based on its connections
std::pair<float, float> AbcGlobalPlacer::calculate_wirelength_gradient(Abc_Obj_t * pObj) {
    assert(Abc_ObjIsNode(pObj));
    float grad_x = 0.0, grad_y = 0.0;
    float xMin, xMax, yMin, yMax;
    Abc_Obj_t * pFanin, * pFaninOut, *pFanout;
    int i, j;
    
    // For each net containing this object, we need to:
    // 1. Find all pins in the net
    // 2. Calculate bounding box
    // 3. Check if this object is on the bounding box boundary
    // 4. If yes, add gradient contribution
    
    // We'll build nets on-the-fly by following fanins/fanouts
    // This is a simplified approach - in practice, ABC might have explicit net structures
    
    // pObj as a sink
    xMin = xMax = pObj->xPos;
    yMin = yMax = pObj->yPos;
    Abc_ObjForEachFanin( pObj, pFanin, i) {
        if (pFanin->Id == 0)
            continue;
        xMin = std::min(xMin, pFanin->xPos);
        xMax = std::max(xMax, pFanin->xPos);
        yMin = std::min(yMin, pFanin->yPos);
        yMax = std::max(yMax, pFanin->yPos);
        Abc_ObjForEachFanout(pFanin, pFaninOut, j) {
            if (pFaninOut->Id == 0)
                continue;
            xMin = std::min(xMin, pFaninOut->xPos);
            xMax = std::max(xMax, pFaninOut->xPos);
            yMin = std::min(yMin, pFaninOut->yPos);
            yMax = std::max(yMax, pFaninOut->yPos);
        }

        // X direction gradient
        if (std::abs(pObj->xPos - xMin) < 1e-9) {
            // pObj is at the left boundary (min_x)
            grad_x -= 1.0;  // Moving right decreases HPWL
        } else if (std::abs(pObj->xPos - xMax) < 1e-9) {
            // pObj is at the right boundary (max_x)
            grad_x += 1.0;  // Moving right increases HPWL
        }
        // If pObj is in the middle, gradient is 0
        
        // Y direction gradient
        if (std::abs(pObj->yPos - yMin) < 1e-9) {
            // pObj is at the bottom boundary (min_y)
            grad_y -= 1.0;  // Moving up decreases HPWL
        } else if (std::abs(pObj->yPos - yMax) < 1e-9) {
            // pObj is at the top boundary (max_y)
            grad_y += 1.0;  // Moving up increases HPWL
        }
        // If pObj is in the middle, gradient is 0
    }

    // pObj as a driver
    xMin = xMax = pObj->xPos;
    yMin = yMax = pObj->yPos;
    Abc_ObjForEachFanout( pObj, pFanout, i) {
        if (pFanout->Id == 0)
            continue;
        xMin = std::min(xMin, pFanout->xPos);
        xMax = std::max(xMax, pFanout->xPos);
        yMin = std::min(yMin, pFanout->yPos);
        yMax = std::max(yMax, pFanout->yPos);

        // X direction gradient
        if (std::abs(pObj->xPos - xMin) < 1e-9) {
            // pObj is at the left boundary (min_x)
            grad_x -= 1.0;  // Moving right decreases HPWL
        } else if (std::abs(pObj->xPos - xMax) < 1e-9) {
            // pObj is at the right boundary (max_x)
            grad_x += 1.0;  // Moving right increases HPWL
        }
        // If pObj is in the middle, gradient is 0
        
        // Y direction gradient
        if (std::abs(pObj->yPos - yMin) < 1e-9) {
            // pObj is at the bottom boundary (min_y)
            grad_y -= 1.0;  // Moving up decreases HPWL
        } else if (std::abs(pObj->yPos - yMax) < 1e-9) {
            // pObj is at the top boundary (max_y)
            grad_y += 1.0;  // Moving up increases HPWL
        }
        // If pObj is in the middle, gradient is 0
    }
    
    return {grad_x, grad_y};
}

// Calculate total wirelength (for monitoring)
float AbcGlobalPlacer::calculate_total_wirelength() {
    float totalHPWL = 0;
    Abc_Obj_t * pObj, * pFanout;
    int i, j;
    float xMin, xMax, yMin, yMax;

    Abc_NtkForEachObj(pNtk, pObj, i) {
        xMin = xMax = pObj->xPos;
        yMin = yMax = pObj->yPos;
        Abc_ObjForEachFanout( pObj, pFanout, j ) {
            if (pFanout->Id == 0)
                continue;
            xMin = std::min(xMin, pFanout->xPos);
            yMin = std::min(yMin, pFanout->yPos);
            xMax = std::max(xMax, pFanout->xPos);
            yMax = std::max(yMax, pFanout->yPos);
        }
        totalHPWL += xMax - xMin + yMax - yMin;
    }
    
    return totalHPWL;
}

// Perform one iteration with pathology detection
void AbcGlobalPlacer::force_directed_iteration(bool fDist) {
    update_density_map();
    
    int nObjs = Abc_NtkObjNum(pNtk);
    
    // Store previous positions for rollback
    std::vector<std::pair<float, float>> prev_positions(nObjs);
    for (int i = 0; i < nObjs; i++) {
        Abc_Obj_t * pObj = Abc_NtkObj(pNtk, i);
        if (pObj) {
            prev_positions[i] = {pObj->xPos, pObj->yPos};
        }
    }
    
    float prev_wirelength = calculate_total_wirelength();
    
    // Clear gradients
    std::fill(grad_x.begin(), grad_x.end(), 0.0);
    std::fill(grad_y.begin(), grad_y.end(), 0.0);
    
    // Calculate forces for each non-terminal object
    for (int i = 0; i < nObjs; i++) {
        Abc_Obj_t * pObj = Abc_NtkObj(pNtk, i);
        if (pObj && Abc_ObjIsNode(pObj)) {
            // Wirelength gradient (attractive force)
            std::pair<float, float> density_grad;
            auto wl_grad = calculate_wirelength_gradient(pObj);
            
            // Density gradient (repulsive force)
            if (!fDist)
                density_grad = calculate_density_gradient(pObj);
            else
                density_grad = calculate_density_gradient_dist(pObj);
            
            // Combined force (negative gradient for minimization)
            // std::cout << '1: ' << wl_grad.first << ' ' << density_grad.first << ' ' << wl_grad.second << ' ' << density_grad.second << std::endl;
            grad_x[i] = -wl_grad.first - alpha * density_grad.first;
            grad_y[i] = -wl_grad.second - alpha * density_grad.second;
            // std::cout << '2: '  << grad_x[i] << ' ' << grad_y[i] << std::endl;
            // Limit force magnitude to prevent explosions
            float force_mag = sqrt(grad_x[i]*grad_x[i] + grad_y[i]*grad_y[i]);
            float max_force = std::min(chip_xMax - chip_xMin, chip_yMax - chip_yMin) / 10.0;
            if (force_mag > max_force) {
                grad_x[i] *= max_force / force_mag;
                grad_y[i] *= max_force / force_mag;
            }
            // std::cout << '3: ' << grad_x[i] << ' ' << grad_y[i] << std::endl;
        }
    }
    
    // Apply forces with strict bounds checking
    for (int i = 0; i < nObjs; i++) {
        Abc_Obj_t * pObj = Abc_NtkObj(pNtk, i);
        if (pObj && Abc_ObjIsNode(pObj)) {
            // Update position
            pObj->xPos += step_size * grad_x[i];
            pObj->yPos += step_size * grad_y[i];
            
            // STRICT boundary enforcement
            float margin_x = pObj->half_den_sizeX; // Extra margin
            float margin_y = pObj->half_den_sizeY;
            
            pObj->xPos = std::max(chip_xMin + margin_x, 
                         std::min(chip_xMax - margin_x, pObj->xPos));
            pObj->yPos = std::max(chip_yMin + margin_y, 
                         std::min(chip_yMax - margin_y, pObj->yPos));
            
            // Sanity check coordinates
            if (pObj->xPos < chip_xMin || pObj->yPos < chip_yMin || 
                pObj->xPos > chip_xMax || pObj->yPos > chip_yMax) {
                std::cout << "COORDINATE ERROR: Object " << pObj->Id 
                          << " at (" << pObj->xPos << ", " << pObj->yPos << ")\n";
                // Reset to previous position
                pObj->xPos = prev_positions[i].first;
                pObj->yPos = prev_positions[i].second;
            }
        }
    }
    
    // Check for pathological behavior
    float new_wirelength = calculate_total_wirelength();
    
    if (new_wirelength < 0) {
        std::cout << "PATHOLOGY DETECTED: Negative wirelength (" << new_wirelength << ")!\n";
        std::cout << "Rolling back and reducing density penalty...\n";
        
        // Rollback all positions
        for (int i = 0; i < nObjs; i++) {
            Abc_Obj_t * pObj = Abc_NtkObj(pNtk, i);
            if (pObj) {
                pObj->xPos = prev_positions[i].first;
                pObj->yPos = prev_positions[i].second;
            }
        }
        
        // Reduce problematic parameters
        alpha *= 0.7;     // Reduce density penalty
        step_size *= 0.5; // Reduce step size
        
        std::cout << "Reduced alpha to " << alpha << ", step_size to " << step_size << std::endl;
    }
    
    // Additional sanity check: if wirelength exploded
    if (new_wirelength > prev_wirelength * 5.0) {
        std::cout << "WIRELENGTH EXPLOSION: " << prev_wirelength << " → " << new_wirelength << std::endl;
        std::cout << "Rolling back...\n";
        
        // Rollback
        for (int i = 0; i < nObjs; i++) {
            Abc_Obj_t * pObj = Abc_NtkObj(pNtk, i);
            if (pObj) {
                pObj->xPos = prev_positions[i].first;
                pObj->yPos = prev_positions[i].second;
            }
        }
        
        step_size *= 0.3;
    }
}

//Main global placement algorithm with aggressive density control
void AbcGlobalPlacer::run_global_placement(int max_iterations = 50, bool fDist = 0) {
    std::cout << "\nStarting ABC global placement...\n";
    
    float prev_wirelength = calculate_total_wirelength();
    std::cout << "Initial wirelength: " << std::fixed << std::setprecision(2) 
                << prev_wirelength << std::endl;
    
    float target_density = 1.0;
    float density_tolerance = 0.1; // 5% tolerance
    
    for (int iter = 0; iter < max_iterations; iter++) {
        force_directed_iteration(fDist);
        
        float current_wirelength = calculate_total_wirelength();
        update_density_map();
        float overflow = get_overflow();
        
        if (iter % 10 == 0 || iter == max_iterations - 1) {
            std::cout << "Iteration " << iter << ": WL = " 
                        << std::fixed << std::setprecision(2) << current_wirelength
                        << ", Overflow = " << std::setprecision(3) << overflow;
            
            if (iter > 0) {
                float improvement = (prev_wirelength - current_wirelength) / prev_wirelength * 100;
                std::cout << " (WL Improvement: " << improvement << "%)";
            }
            std::cout << std::endl;
            std::cout << "StepSize = " << step_size << ", Alpha = " 
                        << std::fixed << std::setprecision(2) << alpha;
            std::cout << std::endl;
            prev_wirelength = current_wirelength;
        }
        
        // Aggressive density penalty adjustment
        if (overflow > density_tolerance) {
            alpha *= 1.2;  // More aggressive than before
            // std::cout << "  Density violation! Increased alpha to " << alpha << std::endl;
        } else {
            alpha *= 0.9; // Reduce penalty when density is good
        }
        
        // Adaptive step size based on progress
        if (iter > 0 && iter % 10 == 0) {
            if (overflow > density_tolerance) {
                // step_size *= 1.05;  // Increase movement when stuck
            } else {
                if ( rand() / (float)RAND_MAX > 0.5 * (1.0 - (double)iter / max_iterations) )
                    step_size *= 0.95;  // Reduce for fine-tuning
                else
                    step_size *= 1.05;
            }
        }
        
        // Early termination if density target achieved
        if (overflow <= density_tolerance) {
            // std::cout << "Density target achieved! Max density: " << overflow << std::endl;
            if (iter > max_iterations * 0.9) break; // Only terminate if reasonably converged
        }
    }
    
    std::cout << "Global placement completed.\n";
    std::cout << "Final wirelength: " << calculate_total_wirelength() << std::endl;
    std::cout << "Final max density: " << get_overflow() << std::endl;
    if ( get_overflow() > density_tolerance )
    std::cout << "Density violated!" << std::endl;
}

// Helper function to get maximum bin density
float AbcGlobalPlacer::get_overflow() {
    float overflow = 0.0;
    for (int i = 0; i < pNtk->nBins; i++) {
        overflow += std::max(0.0, pNtk->binsDensity[i] - 1.0);
    }
    overflow *= pNtk->binModuleRatio;
    return overflow;
}

// Export placement results for visualization
void AbcGlobalPlacer::export_placement_results(const char* filename) {
    FILE* fp = fopen(filename, "w");
    if (!fp) {
        std::cout << "Cannot open file " << filename << " for writing.\n";
        return;
    }
    
    fprintf(fp, "# ABC Placement Results\n");
    fprintf(fp, "# Format: ObjID xPos yPos half_sizeX half_sizeY isTerminal\n");
    
    int nObjs = Abc_NtkObjNum(pNtk);
    for (int i = 0; i < nObjs; i++) {
        Abc_Obj_t * pObj = Abc_NtkObj(pNtk, i);
        if (pObj) {
            fprintf(fp, "%d %.3f %.3f %.3f %.3f %d\n", 
                    pObj->Id, pObj->xPos, pObj->yPos,
                    pObj->half_den_sizeX, pObj->half_den_sizeY,
                    Abc_ObjIsTerm(pObj));
        }
    }
    
    fclose(fp);
    std::cout << "Placement results exported to: " << filename << std::endl;
}
























// class AbcGlobalPlacer {
// private:
//     Abc_Ntk_t * pNtk;
    
//     // Placement parameters
//     float alpha;           // Force scaling factor for density
//     float step_size;       // Movement step size
//     float chip_width;      // Chip dimensions
//     float chip_height;
    
//     // Temporary storage for gradients
//     std::vector<float> grad_x;
//     std::vector<float> grad_y;
    
//     // Helper function to calculate HPWL gradient for a specific object in a specific net
//     std::pair<float, float> calculate_hpwl_gradient_in_net(Abc_Obj_t * pObj, 
//                                                            float min_x, float max_x, 
//                                                            float min_y, float max_y);
    
// public:
//     AbcGlobalPlacer(Abc_Ntk_t * pNetwork, float width, float height) 
//         : pNtk(pNetwork), alpha(1.0), step_size(10.0), 
//           chip_width(width), chip_height(height) {
        
//         int nObjs = Abc_NtkObjNum(pNtk);
//         grad_x.resize(nObjs, 0.0);
//         grad_y.resize(nObjs, 0.0);
        
//         // Initialize bin parameters if not set
//         if (pNtk->binDimX == 0 || pNtk->binDimY == 0) {
//             pNtk->binDimX = std::max(10, (int)(width / 100.0));
//             pNtk->binDimY = std::max(10, (int)(height / 100.0));
//             pNtk->binStepX = width / pNtk->binDimX;
//             pNtk->binStepY = height / pNtk->binDimY;
//             pNtk->binOriX = 0.0;
//             pNtk->binOriY = 0.0;
//             pNtk->nBins = pNtk->binDimX * pNtk->binDimY;
            
//             // Allocate density array if not already allocated
//             if (pNtk->binsDensity == nullptr) {
//                 pNtk->binsDensity = new float[pNtk->nBins];
//                 memset(pNtk->binsDensity, 0, pNtk->nBins * sizeof(float));
//             }
            
//             pNtk->invBinArea = 1.0 / (pNtk->binStepX * pNtk->binStepY);
//         }
        
//         std::cout << "ABC Global Placer initialized:\n";
//         std::cout << "  Network objects: " << nObjs << std::endl;
//         std::cout << "  Chip size: " << width << " x " << height << std::endl;
//         std::cout << "  Bin grid: " << pNtk->binDimX << " x " << pNtk->binDimY << std::endl;
//     }
    
//     ~AbcGlobalPlacer() {
//         // Note: Don't delete pNtk->binsDensity here as it's managed by ABC
//     }
    
//     // Initialize random placement for non-terminal nodes
//     void random_initial_placement() {
//         Abc_Obj_t * pNode;
//         int i;
        
//         srand(12345); // Fixed seed for reproducibility
//         int placed_count = 0;
//         Abc_NtkForEachNode(pNtk, pNode, i)
//             if (!Abc_ObjIsTerm(pNode)) {
//                 // Random placement within chip bounds, considering object size 
//                 pNode->xPos = pNtk->binOriX + pNode->half_den_sizeX + (rand() / (float)RAND_MAX) * (pNtk->binDimX * pNtk->binStepX - 2 * pNode->half_den_sizeX);
//                 pNode->yPos = pNtk->binOriY + pNode->half_den_sizeY + (rand() / (float)RAND_MAX) * (pNtk->binDimY * pNtk->binStepY - 2 * pNode->half_den_sizeY);
//                 placed_count++;
//             }
        
//         std::cout << "Randomly placed " << placed_count << " non-terminal nodes." << std::endl;
//     }
    
//     // Update density map based on current object positions
//     void update_density_map() {
//         // Clear density map
//         memset(pNtk->binsDensity, 0, pNtk->nBins * sizeof(float));
        
//         float xMin, yMin, xMax, yMax;
//         int xb0, xb1, yb0, yb1;
//         float x0, x1, y0, y1;
//         float xShare[10], yShare[10];
//         Abc_Obj_t * pNode;
//         int i;

//         Abc_NtkForEachObj(pNtk, pNode, i)
//         {
//             xMin = pNode->xPos - pNode->half_den_sizeX;
//             yMin = pNode->yPos - pNode->half_den_sizeY;
//             xMax = pNode->xPos + pNode->half_den_sizeX;
//             yMax = pNode->yPos + pNode->half_den_sizeY;
//             xb0 = (int)((xMin - pNtk->binOriX) / pNtk->binStepX);
//             yb0 = (int)((yMin - pNtk->binOriY) / pNtk->binStepY);
//             xb1 = (int)((xMax - pNtk->binOriX) / pNtk->binStepX);
//             yb1 = (int)((yMax - pNtk->binOriY) / pNtk->binStepY);
//             if (xb0 < 0)
//                 xb0 = 0;
//             if (yb0 < 0)
//                 yb0 = 0;
//             if (xb1 >= pNtk->binDimX)
//                 xb1 = pNtk->binDimX - 1;
//             if (yb1 >= pNtk->binDimY)
//                 yb1 = pNtk->binDimY - 1;
            
//             for( int xIdx = xb0; xIdx <= xb1; xIdx++) {
//                 x0 = xIdx * pNtk->binStepX + pNtk->binOriX;

//                 x0 = std::max(x0, xMin);

//                 x1 = (xIdx + 1) * pNtk->binStepX + pNtk->binOriX;

//                 x1 = std::min(x1, xMax);

//                 xShare[xIdx - xb0] = x1 - x0;
//             }

//             for( int yIdx = yb0; yIdx <= yb1; yIdx++) {
//                 y0 = yIdx * pNtk->binStepY + pNtk->binOriY;

//                 y0 = std::max(y0, yMin);

//                 y1 = (yIdx + 1) * pNtk->binStepY + pNtk->binOriY;

//                 y1 = std::min(y1, yMax);

//                 yShare[yIdx - yb0] = y1 - y0;
//             }


//             for(int xIdx = xb0; xIdx <= xb1; xIdx++) {
//                 for(int yIdx = yb0; yIdx <= yb1; yIdx++) {
//                     pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] += xShare[xIdx - xb0] * yShare[yIdx - yb0] * pNtk->invBinArea * pNode->den_scal;
//                 }
//             }
//         }
//     }
    
//     // Calculate density gradient for an object
//     std::pair<float, float> calculate_density_gradient(Abc_Obj_t * pObj) {
//         int bin_xMin = (int)(pObj->xPos - pObj->half_den_sizeX - pNtk->binOriX) / pNtk->binStepX;
//         int bin_yMin = (int)(pObj->yPos - pObj->half_den_sizeY - pNtk->binOriY) / pNtk->binStepY;
//         int bin_xMax = (int)(pObj->xPos + pObj->half_den_sizeX - pNtk->binOriX) / pNtk->binStepX;
//         int bin_yMax = (int)(pObj->yPos + pObj->half_den_sizeY - pNtk->binOriY) / pNtk->binStepY;
        
//         float grad_x = 0.0, grad_y = 0.0;

//         int bottom_idx, top_idx;
//         int left_idx, right_idx;
//         float overflow_bottom, overflow_top, overflow_left, overflow_right, overflow_avg = 0.0;

//         float x0, x1, y0, y1;
//         float xShare[10], yShare[10];

//         assert( bin_xMin >= 0 && bin_xMax <= pNtk->binDimX - 1 && bin_xMax > bin_xMin);
//         assert( bin_yMin >= 0 && bin_yMax <= pNtk->binDimY - 1 && bin_yMax > bin_yMin);
        
//         // Calculate average overflow
//         for( int xIdx = bin_xMin; xIdx <= bin_xMax; xIdx++) {
//             x0 = xIdx * pNtk->binStepX + pNtk->binOriX;

//             x0 = std::max(x0, pObj->xPos - pObj->half_den_sizeX);

//             x1 = (xIdx + 1) * pNtk->binStepX + pNtk->binOriX;

//             x1 = std::min(x1, pObj->xPos + pObj->half_den_sizeX);

//             xShare[xIdx - bin_xMin] = x1 - x0;
//         }

//         for( int yIdx = bin_yMin; yIdx <= bin_yMax; yIdx++) {
//             y0 = yIdx * pNtk->binStepY + pNtk->binOriY;

//             y0 = std::max(y0, pObj->yPos - pObj->half_den_sizeY);

//             y1 = (yIdx + 1) * pNtk->binStepY + pNtk->binOriY;

//             y1 = std::min(y1, pObj->yPos + pObj->half_den_sizeY);

//             yShare[yIdx - bin_yMin] = y1 - y0;
//         }


//         for(int xIdx = bin_xMin; xIdx <= bin_xMax; xIdx++) {
//             for(int yIdx = bin_yMin; yIdx <= bin_yMax; yIdx++) {
//                 overflow_avg += std::max(0.0, pNtk->binsDensity[xIdx * pNtk->binDimY + yIdx] - 1.0) * xShare[xIdx - bin_xMin] * yShare[yIdx - bin_xMax];
//             }
//         }
//         overflow_avg /= 4 * pObj->half_den_sizeX * pObj->half_den_sizeY;

//         // Calculate gradient using central difference in neighboring bins
//         for (int i = bin_yMin; i <= bin_yMax; ++i)
//         {
//             bottom_idx = bin_xMin * pNtk->binDimX + i;
//             top_idx = bin_xMax * pNtk->binDimX + i;
//             overflow_bottom = std::max(0.0, pNtk->binsDensity[bottom_idx] - 1.0);
//             overflow_top = std::max(0.0, pNtk->binsDensity[top_idx] - 1.0);

//             grad_x += (overflow_top - overflow_bottom + (pNtk->binsDensity[top_idx] - pNtk->binsDensity[bottom_idx]) * overflow_avg) * pObj->den_scal;
//         }

//         for (int i = bin_xMin; i <= bin_xMax; ++i)
//         {
//             left_idx = i * pNtk->binDimX + bin_yMin;
//             right_idx = i * pNtk->binDimX + bin_yMax;
//             overflow_left = std::max(0.0, pNtk->binsDensity[left_idx] - 1.0);
//             overflow_right = std::max(0.0, pNtk->binsDensity[right_idx] - 1.0);

//             grad_y += (overflow_right - overflow_left + (pNtk->binsDensity[right_idx] - pNtk->binsDensity[left_idx]) * overflow_avg) * pObj->den_scal;
//         }

//         return {grad_x, grad_y};
//     }
    
//     // Calculate HPWL gradient for an object based on its connections
//     std::pair<float, float> calculate_wirelength_gradient(Abc_Obj_t * pObj) {
//         float grad_x = 0.0, grad_y = 0.0;

//         float xMin, xMax, yMin, yMax;
//         Abc_Obj_t * pFanin, * pFaninOut, *pFanout;
//         int i, j;
        
//         // For each net containing this object, we need to:
//         // 1. Find all pins in the net
//         // 2. Calculate bounding box
//         // 3. Check if this object is on the bounding box boundary
//         // 4. If yes, add gradient contribution
        
//         // We'll build nets on-the-fly by following fanins/fanouts
//         // This is a simplified approach - in practice, ABC might have explicit net structures
        
//         // pObj as a sink
//         xMin = xMax = pObj->xPos;
//         yMin = yMax = pObj->yPos;
//         Abc_ObjForEachFanin( pObj, pFanin, i)
//         {
//             if (pFanin->Id == 0)
//                 continue;
//             xMin = std::min(xMin, pFanin->xPos);
//             xMax = std::max(xMax, pFanin->xPos);
//             yMin = std::min(yMin, pFanin->yPos);
//             yMax = std::max(yMax, pFanin->yPos);
//             Abc_ObjForEachFanout(pFanin, pFaninOut, j)
//             {
//                 if (pFaninOut->Id == 0)
//                     continue;
//                 xMin = std::min(xMin, pFaninOut->xPos);
//                 xMax = std::max(xMax, pFaninOut->xPos);
//                 yMin = std::min(yMin, pFaninOut->yPos);
//                 yMax = std::max(yMax, pFaninOut->yPos);
//             }

//             // X direction gradient
//             if (std::abs(pObj->xPos - xMin) < 1e-9) {
//                 // pObj is at the left boundary (min_x)
//                 grad_x -= 1.0;  // Moving right decreases HPWL
//             } else if (std::abs(pObj->xPos - xMax) < 1e-9) {
//                 // pObj is at the right boundary (max_x)
//                 grad_x += 1.0;  // Moving right increases HPWL
//             }
//             // If pObj is in the middle, gradient is 0
            
//             // Y direction gradient
//             if (std::abs(pObj->yPos - yMin) < 1e-9) {
//                 // pObj is at the bottom boundary (min_y)
//                 grad_y -= 1.0;  // Moving up decreases HPWL
//             } else if (std::abs(pObj->yPos - yMax) < 1e-9) {
//                 // pObj is at the top boundary (max_y)
//                 grad_y += 1.0;  // Moving up increases HPWL
//             }
//             // If pObj is in the middle, gradient is 0
//         }

//         // pObj as a driver
//         xMin = xMax = pObj->xPos;
//         yMin = yMax = pObj->yPos;
//         Abc_ObjForEachFanout( pObj, pFanout, i)
//         {
//             if (pFanout->Id == 0)
//                 continue;
//             xMin = std::min(xMin, pFanout->xPos);
//             xMax = std::max(xMax, pFanout->xPos);
//             yMin = std::min(yMin, pFanout->yPos);
//             yMax = std::max(yMax, pFanout->yPos);

//             // X direction gradient
//             if (std::abs(pObj->xPos - xMin) < 1e-9) {
//                 // pObj is at the left boundary (min_x)
//                 grad_x -= 1.0;  // Moving right decreases HPWL
//             } else if (std::abs(pObj->xPos - xMax) < 1e-9) {
//                 // pObj is at the right boundary (max_x)
//                 grad_x += 1.0;  // Moving right increases HPWL
//             }
//             // If pObj is in the middle, gradient is 0
            
//             // Y direction gradient
//             if (std::abs(pObj->yPos - yMin) < 1e-9) {
//                 // pObj is at the bottom boundary (min_y)
//                 grad_y -= 1.0;  // Moving up decreases HPWL
//             } else if (std::abs(pObj->yPos - yMax) < 1e-9) {
//                 // pObj is at the top boundary (max_y)
//                 grad_y += 1.0;  // Moving up increases HPWL
//             }
//             // If pObj is in the middle, gradient is 0
//         }
        
//         return {grad_x, grad_y};
//     }
    
//     // Calculate total wirelength (for monitoring)
//     float calculate_total_wirelength() {
//         float totalHPWL = 0;
//         Abc_Obj_t * pObj, * pFanout;
//         int i, j;
//         float xMin, xMax, yMin, yMax;

//         Abc_NtkForEachObj(pNtk, pObj, i)
//         {
//             xMin = xMax = pObj->xPos;
//             yMin = yMax = pObj->yPos;
//             Abc_ObjForEachFanout( pObj, pFanout, j )
//             {
//                 if (pFanout->Id == 0)
//                     continue;
//                 xMin = std::min(xMin, pFanout->xPos);
//                 yMin = std::min(yMin, pFanout->yPos);
//                 xMax = std::max(xMax, pFanout->xPos);
//                 yMax = std::max(yMax, pFanout->yPos);
//             }
//             totalHPWL += xMax - xMin + yMax - yMin;
//         }
        
//         return totalHPWL;
//     }
    
//     // Perform one iteration with pathology detection
//     void force_directed_iteration() {
//         update_density_map();
        
//         int nObjs = Abc_NtkObjNum(pNtk);
        
//         // Store previous positions for rollback
//         std::vector<std::pair<float, float>> prev_positions(nObjs);
//         for (int i = 0; i < nObjs; i++) {
//             Abc_Obj_t * pObj = Abc_NtkObj(pNtk, i);
//             if (pObj) {
//                 prev_positions[i] = {pObj->xPos, pObj->yPos};
//             }
//         }
        
//         float prev_wirelength = calculate_total_wirelength();
        
//         // Clear gradients
//         std::fill(grad_x.begin(), grad_x.end(), 0.0);
//         std::fill(grad_y.begin(), grad_y.end(), 0.0);
        
//         // Calculate forces for each non-terminal object
//         for (int i = 0; i < nObjs; i++) {
//             Abc_Obj_t * pObj = Abc_NtkObj(pNtk, i);
//             if (pObj && !Abc_ObjIsTerm(pObj)) {
//                 // Wirelength gradient (attractive force)
//                 auto wl_grad = calculate_wirelength_gradient(pObj);
                
//                 // Density gradient (repulsive force)
//                 auto density_grad = calculate_density_gradient(pObj);
                
//                 // Combined force (negative gradient for minimization)
//                 grad_x[i] = -wl_grad.first - alpha * density_grad.first;
//                 grad_y[i] = -wl_grad.second - alpha * density_grad.second;
                
//                 // Limit force magnitude to prevent explosions
//                 float force_mag = sqrt(grad_x[i]*grad_x[i] + grad_y[i]*grad_y[i]);
//                 float max_force = std::min(chip_width, chip_height) / 10.0;
//                 if (force_mag > max_force) {
//                     grad_x[i] *= max_force / force_mag;
//                     grad_y[i] *= max_force / force_mag;
//                 }
//             }
//         }
        
//         // Apply forces with strict bounds checking
//         for (int i = 0; i < nObjs; i++) {
//             Abc_Obj_t * pObj = Abc_NtkObj(pNtk, i);
//             if (pObj && !Abc_ObjIsTerm(pObj)) {
//                 // Update position
//                 pObj->xPos += step_size * grad_x[i];
//                 pObj->yPos += step_size * grad_y[i];
                
//                 // STRICT boundary enforcement
//                 float margin_x = pObj->half_den_sizeX; // Extra margin
//                 float margin_y = pObj->half_den_sizeY;
                
//                 pObj->xPos = std::max(margin_x, 
//                              std::min(chip_width - margin_x, pObj->xPos));
//                 pObj->yPos = std::max(margin_y, 
//                              std::min(chip_height - margin_y, pObj->yPos));
                
//                 // Sanity check coordinates
//                 if (pObj->xPos < 0 || pObj->yPos < 0 || 
//                     pObj->xPos > chip_width || pObj->yPos > chip_height) {
//                     std::cout << "COORDINATE ERROR: Object " << pObj->Id 
//                               << " at (" << pObj->xPos << ", " << pObj->yPos << ")\n";
//                     // Reset to previous position
//                     pObj->xPos = prev_positions[i].first;
//                     pObj->yPos = prev_positions[i].second;
//                 }
//             }
//         }
        
//         // Check for pathological behavior
//         float new_wirelength = calculate_total_wirelength();
        
//         if (new_wirelength < 0) {
//             std::cout << "PATHOLOGY DETECTED: Negative wirelength (" << new_wirelength << ")!\n";
//             std::cout << "Rolling back and reducing density penalty...\n";
            
//             // Rollback all positions
//             for (int i = 0; i < nObjs; i++) {
//                 Abc_Obj_t * pObj = Abc_NtkObj(pNtk, i);
//                 if (pObj) {
//                     pObj->xPos = prev_positions[i].first;
//                     pObj->yPos = prev_positions[i].second;
//                 }
//             }
            
//             // Reduce problematic parameters
//             alpha *= 0.7;     // Reduce density penalty
//             step_size *= 0.5; // Reduce step size
            
//             std::cout << "Reduced alpha to " << alpha << ", step_size to " << step_size << std::endl;
//         }
        
//         // Additional sanity check: if wirelength exploded
//         if (new_wirelength > prev_wirelength * 5.0) {
//             std::cout << "WIRELENGTH EXPLOSION: " << prev_wirelength << " → " << new_wirelength << std::endl;
//             std::cout << "Rolling back...\n";
            
//             // Rollback
//             for (int i = 0; i < nObjs; i++) {
//                 Abc_Obj_t * pObj = Abc_NtkObj(pNtk, i);
//                 if (pObj) {
//                     pObj->xPos = prev_positions[i].first;
//                     pObj->yPos = prev_positions[i].second;
//                 }
//             }
            
//             step_size *= 0.3;
//         }
//     }
    
//     // Main global placement algorithm with aggressive density control
//     void run_global_placement(int max_iterations = 50) {
//         std::cout << "\nStarting ABC global placement...\n";
        
//         float prev_wirelength = calculate_total_wirelength();
//         std::cout << "Initial wirelength: " << std::fixed << std::setprecision(2) 
//                   << prev_wirelength << std::endl;
        
//         float target_density = 1.0;
//         float density_tolerance = 0.1; // 5% tolerance
        
//         for (int iter = 0; iter < max_iterations; iter++) {
//             force_directed_iteration();
            
//             float current_wirelength = calculate_total_wirelength();
//             float overflow = get_overflow();
            
//             if (iter % 10 == 0 || iter == max_iterations - 1) {
//                 std::cout << "Iteration " << iter << ": WL = " 
//                           << std::fixed << std::setprecision(2) << current_wirelength
//                           << ", Overflow = " << std::setprecision(3) << overflow;
                
//                 if (iter > 0) {
//                     float improvement = (prev_wirelength - current_wirelength) / prev_wirelength * 100;
//                     std::cout << " (WL Improvement: " << improvement << "%)";
//                 }
//                 std::cout << std::endl;
//             }
            
//             // Aggressive density penalty adjustment
//             if (overflow > density_tolerance) {
//                 alpha *= 1.3;  // More aggressive than before
//                 std::cout << "  Density violation! Increased alpha to " << alpha << std::endl;
//             } else {
//                 alpha *= 0.95; // Reduce penalty when density is good
//             }
            
//             // Adaptive step size based on progress
//             if (iter > 0 && iter % 10 == 0) {
//                 if (overflow > density_tolerance) {
//                     step_size *= 1.1;  // Increase movement when stuck
//                 } else {
//                     step_size *= 0.9;  // Reduce for fine-tuning
//                 }
//                 prev_wirelength = current_wirelength;
//             }
            
//             // Early termination if density target achieved
//             if (overflow <= density_tolerance) {
//                 std::cout << "Density target achieved! Max density: " << overflow << std::endl;
//                 if (iter > max_iterations * 0.7) break; // Only terminate if reasonably converged
//             }
//         }
        
//         std::cout << "Global placement completed.\n";
//         std::cout << "Final wirelength: " << calculate_total_wirelength() << std::endl;
//         std::cout << "Final max density: " << get_max_bin_density() << std::endl;
//     }
    
//     // Helper function to get maximum bin density
//     float get_overflow() {
//         float overflow = 0.0;
//         for (int i = 0; i < pNtk->nBins; i++) {
//             overflow += std::max(0.0, pNtk->binsDensity[i] - 1.0);
//         }
//         overflow /= pNtk->nBins;
//         return overflow;
//     }
    
//     // Export placement results for visualization
//     void export_placement_results(const char* filename) {
//         FILE* fp = fopen(filename, "w");
//         if (!fp) {
//             std::cout << "Cannot open file " << filename << " for writing.\n";
//             return;
//         }
        
//         fprintf(fp, "# ABC Placement Results\n");
//         fprintf(fp, "# Format: ObjID xPos yPos half_sizeX half_sizeY isTerminal\n");
        
//         int nObjs = Abc_NtkObjNum(pNtk);
//         for (int i = 0; i < nObjs; i++) {
//             Abc_Obj_t * pObj = Abc_NtkObj(pNtk, i);
//             if (pObj) {
//                 fprintf(fp, "%d %.3f %.3f %.3f %.3f %d\n", 
//                        pObj->Id, pObj->xPos, pObj->yPos,
//                        pObj->half_den_sizeX, pObj->half_den_sizeY,
//                        Abc_ObjIsTerm(pObj));
//             }
//         }
        
//         fclose(fp);
//         std::cout << "Placement results exported to: " << filename << std::endl;
//     }
// };





// // Main ABC integration function
// extern "C" int AbcCommandGlobalPlace(Abc_Ntk_t * pNtk, int argc, char ** argv) {
//     // Default parameters
//     float chip_width = 1000.0;
//     float chip_height = 1000.0;
//     int max_iterations = 50;
    
//     // Simple argument parsing
//     for (int i = 0; i < argc; i++) {
//         if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
//             chip_width = atof(argv[i + 1]);
//         }
//         else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
//             chip_height = atof(argv[i + 1]);
//         }
//         else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
//             max_iterations = atoi(argv[i + 1]);
//         }
//     }
    
//     if (!pNtk) {
//         std::cout << "Error: No network loaded.\n";
//         return 1;
//     }
    
//     // Create and run placer
//     AbcGlobalPlacer placer(pNtk, chip_width, chip_height);
    
//     // Initialize with random placement
//     placer.random_initial_placement();
    
//     // Run global placement
//     placer.run_global_placement(max_iterations);
    
//     // Export results
//     placer.export_placement_results("abc_placement.txt");
    
//     return 0;
// }

// // Usage information
// extern "C" void AbcCommandGlobalPlaceUsage() {
//     std::cout << "Usage: global_place [-w width] [-h height] [-n iterations]\n";
//     std::cout << "  -w width     : Chip width (default: 1000)\n";
//     std::cout << "  -h height    : Chip height (default: 1000)\n"; 
//     std::cout << "  -n iterations: Maximum iterations (default: 50)\n";
//     std::cout << "Description:\n";
//     std::cout << "  Performs global placement on the current network.\n";
//     std::cout << "  Updates xPos and yPos fields of non-terminal objects.\n";
// }

// // Test function (can be removed in production)
// int main() {
//     std::cout << "ABC Global Placer Module Compiled Successfully\n";
//     std::cout << "This module should be linked with ABC for actual usage.\n";
//     return 0;
// }