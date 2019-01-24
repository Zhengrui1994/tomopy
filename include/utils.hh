//  Copyright (c) 2015, UChicago Argonne, LLC. All rights reserved.
//  Copyright 2015. UChicago Argonne, LLC. This software was produced
//  under U.S. Government contract DE-AC02-06CH11357 for Argonne National
//  Laboratory (ANL), which is operated by UChicago Argonne, LLC for the
//  U.S. Department of Energy. The U.S. Government has rights to use,
//  reproduce, and distribute this software.  NEITHER THE GOVERNMENT NOR
//  UChicago Argonne, LLC MAKES ANY WARRANTY, EXPRESS OR IMPLIED, OR
//  ASSUMES ANY LIABILITY FOR THE USE OF THIS SOFTWARE.  If software is
//  modified to produce derivative works, such modified software should
//  be clearly marked, so as not to confuse it with the version available
//  from ANL.
//  Additionally, redistribution and use in source and binary forms, with
//  or without modification, are permitted provided that the following
//  conditions are met:
//      * Redistributions of source code must retain the above copyright
//        notice, this list of conditions and the following disclaimer.
//      * Redistributions in binary form must reproduce the above copyright
//        notice, this list of conditions and the following disclaimer in
//        the documentation andwith the
//        distribution.
//      * Neither the name of UChicago Argonne, LLC, Argonne National
//        Laboratory, ANL, the U.S. Government, nor the names of its
//        contributors may be used to endorse or promote products derived
//        from this software without specific prior written permission.
//  THIS SOFTWARE IS PROVIDED BY UChicago Argonne, LLC AND CONTRIBUTORS
//  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
//  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
//  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL UChicago
//  Argonne, LLC OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
//  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
//  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
//  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
//  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
//  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
//  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
//  POSSIBILITY OF SUCH DAMAGE.
//  ---------------------------------------------------------------
//   TOMOPY class header

#pragma once

//======================================================================================//

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef TOMOPY_USE_TIMEMORY
#    include <timemory/timemory.hpp>
#else
#    include "profiler.hh"
#endif

#include "gpu.hh"

#include "PTL/AutoLock.hh"
#include "PTL/TaskManager.hh"
#include "PTL/TaskRunManager.hh"
#include "PTL/ThreadData.hh"
#include "PTL/Threading.hh"
#include "PTL/Utility.hh"

#if !defined(scast)
#    define scast static_cast
#endif

#if !defined(PRINT_HERE)
#    define PRINT_HERE(extra)                                                            \
        printf("[%lu]> %s@'%s':%i %s\n", GetThisThreadID(), __FUNCTION__, __FILE__,      \
               __LINE__, extra)
#endif

#if !defined(NUM_TASK_THREADS)
#    define NUM_TASK_THREADS Thread::hardware_concurrency()
#endif

#if defined(DEBUG)
#    define PRINT_MAX_ITER 1
#    define PRINT_MAX_SLICE 1
#    define PRINT_MAX_ANGLE 1
#    define PRINT_MAX_PIXEL 5
#else
#    define PRINT_MAX_ITER 0
#    define PRINT_MAX_SLICE 0
#    define PRINT_MAX_ANGLE 0
#    define PRINT_MAX_PIXEL 0
#endif

#define _forward_args_t(_Args, _args) std::forward<_Args>(_args)...

//======================================================================================//

typedef std::vector<float> farray_t;
typedef std::vector<int>   iarray_t;

//======================================================================================//

struct GpuOption
{
    int         index;
    std::string key;
    std::string description;

    static void spacer(std::ostream& os, const char c = '-')
    {
        std::stringstream ss;
        ss.fill(c);
        ss << std::setw(90) << ""
           << "\n";
        os << ss.str();
    }

    static void header(std::ostream& os)
    {
        std::stringstream ss;
        ss << "\n";
        spacer(ss, '=');
        ss << "Available GPU options:\n";
        ss << "\t" << std::left << std::setw(5) << "INDEX"
           << "  \t" << std::left << std::setw(12) << "KEY"
           << "  " << std::left << std::setw(40) << "DESCRIPTION"
           << "\n";
        os << ss.str();
    }

    static void footer(std::ostream& os)
    {
        std::stringstream ss;
        ss << "\nTo select an option for runtime, set TOMOPY_GPU_TYPE "
           << "environment variable\n  to an INDEX or KEY above\n";
        spacer(ss, '=');
        os << ss.str();
    }

    friend std::ostream& operator<<(std::ostream& os, const GpuOption& opt)
    {
        std::stringstream ss;
        ss << "\t" << std::right << std::setw(5) << opt.index << "  \t" << std::left
           << std::setw(12) << opt.key << "  " << std::left << std::setw(40)
           << opt.description;
        os << ss.str();
        return os;
    }
};

//======================================================================================//

template <typename _Tp>
_Tp
from_string(const std::string& val)
{
    std::stringstream ss;
    _Tp               ret;
    ss << val;
    ss >> ret;
    return ret;
}

//======================================================================================//

inline std::string
tolower(std::string val)
{
    for(uintmax_t i = 0; i < val.size(); ++i)
        val[i] = tolower(val[i]);
    return val;
}

//======================================================================================//

inline void
init_thread_data(ThreadPool* tp)
{
    ThreadData*& thread_data = ThreadData::GetInstance();
    if(!thread_data)
        thread_data = new ThreadData(tp);
    thread_data->is_master   = false;
    thread_data->within_task = false;
}

//======================================================================================//

template <typename _Tp>
void
print_cpu_array(const uintmax_t& nx, const uintmax_t& ny, const _Tp* data, const int& itr,
                const int& slice, const int& angle, const int& pixel,
                const std::string& tag)
{
    std::ofstream     ofs;
    std::stringstream fname;
    fname << "outputs/cpu/" << tag << "_" << itr << "_" << slice << "_" << angle << "_"
          << pixel << ".dat";
    std::stringstream ss;
    for(uintmax_t j = 0; j < ny; ++j)
    {
        for(uintmax_t i = 0; i < nx; ++i)
        {
            ss << std::setw(6) << i << " \t " << std::setw(12) << std::setprecision(8)
               << data[i + j * nx] << std::endl;
        }
        ss << std::endl;
    }
    ofs.open(fname.str().c_str());
    if(!ofs)
        return;
    ofs << ss.str() << std::endl;
    ofs.close();
}

//======================================================================================//

inline TaskRunManager*&
cpu_run_manager()
{
    static TaskRunManager* _instance =
        new TaskRunManager(GetEnv<bool>("TOMOPY_USE_TBB", false, "Enable TBB backend"));
    return _instance;
}

//======================================================================================//

inline TaskRunManager*&
gpu_run_manager()
{
    AutoLock               l(TypeMutex<TaskRunManager>());
    static TaskRunManager* _instance =
        new TaskRunManager(GetEnv<bool>("TOMOPY_USE_TBB", false, "Enable TBB backend"));
    return _instance;
}

//======================================================================================//

inline Mutex&
update_mutex()
{
    static Mutex _instance;
    return _instance;
}

//======================================================================================//

inline void
init_run_manager(TaskRunManager*& run_man, uintmax_t nthreads)
{
    auto tid = GetThisThreadID();
    ConsumeParameters(tid);

    {
        AutoLock l(TypeMutex<TaskRunManager>());
        if(!run_man->IsInitialized())
        {
            std::cout << "\n"
                      << "[" << tid << "] Initializing tasking run manager with "
                      << nthreads << " threads..." << std::endl;
            run_man->Initialize(nthreads);
        }
    }
    TaskManager* task_man = run_man->GetTaskManager();
    ThreadPool*  tp       = task_man->thread_pool();
    init_thread_data(tp);

    if(GetEnv<int>("TASKING_VERBOSE", 0) > 0)
    {
        AutoLock l(TypeMutex<decltype(std::cout)>());
        std::cout << "> " << __FUNCTION__ << "@" << __LINE__ << " -- "
                  << "run manager = " << run_man << ", "
                  << "task manager = " << task_man << ", "
                  << "thread pool = " << tp << ", "
                  << "..." << std::endl;
    }
}

//======================================================================================//

DLL void
cxx_affine_transform(farray_t& dst, const float* src, float theta, const int nx,
                     const int ny, const int factor);

//======================================================================================//

DLL float
bilinear_interpolation(float x, float y, float x1, float x2, float y1, float y2,
                       float x1y1, float x2y1, float x1y2, float x2y2);

//======================================================================================//

DLL farray_t
    cxx_rotate(const float* src, float theta, const int nx, const int ny);

//======================================================================================//

DLL void
cxx_rotate_ip(farray_t& dst, const float* src, float theta, const int nx, const int ny);

//======================================================================================//

template <typename _Func, typename... _Args>
void
run_gpu_algorithm(_Func cpu_func, _Func cuda_func, _Func acc_func, _Func omp_func,
                  _Args... args)
{
    std::deque<GpuOption> options;
    int                   default_idx = 0;
    std::string           default_key = "cpu";

#if defined(TOMOPY_USE_CUDA)
    options.push_back(GpuOption({ 1, "cuda", "Run with CUDA" }));
#endif

#if defined(TOMOPY_USE_OPENACC)
    options.push_back(GpuOption({ 2, "openacc", "Run with OpenACC" }));
#endif

#if defined(TOMOPY_USE_OPENMP)
    options.push_back(GpuOption({ 3, "openmp", "Run with OpenMP" }));
#endif

    //------------------------------------------------------------------------//
    auto print_options = [&]() {
        static bool first = true;
        if(!first)
            return;
        else
            first = false;

        std::stringstream ss;
        GpuOption::header(ss);
        for(const auto& itr : options)
            ss << itr << "\n";
        GpuOption::footer(ss);

        AutoLock l(TypeMutex<decltype(std::cout)>());
        std::cout << "\n" << ss.str() << std::endl;
    };
    //------------------------------------------------------------------------//
    auto print_selection = [&](GpuOption& selected_opt) {
        static bool first = true;
        if(!first)
            return;
        else
            first = false;

        std::stringstream ss;
        GpuOption::spacer(ss, '-');
        ss << "Selected device: " << selected_opt << "\n";
        GpuOption::spacer(ss, '-');

        AutoLock l(TypeMutex<decltype(std::cout)>());
        std::cout << ss.str() << std::endl;
    };
    //------------------------------------------------------------------------//

    // Run on CPU if nothing available
    if(options.size() == 0)
    {
        cpu_func(_forward_args_t(_Args, args));
        return;
    }

    // print the GPU execution type options
    print_options();

    default_idx = options.front().index;
    default_key = options.front().key;
    auto key    = GetEnv("TOMOPY_GPU_TYPE", default_key);

    int selection = default_idx;
    for(auto itr : options)
    {
        if(key == tolower(itr.key) || from_string<int>(key) == itr.index)
        {
            selection = itr.index;
            print_selection(itr);
        }
    }

    try
    {
        if(selection == 1)
        {
            cuda_func(_forward_args_t(_Args, args));
        }
        else if(selection == 2)
        {
            acc_func(_forward_args_t(_Args, args));
        }
        else if(selection == 3)
        {
            omp_func(_forward_args_t(_Args, args));
        }
    }
    catch(std::exception& e)
    {
        {
            AutoLock l(TypeMutex<decltype(std::cout)>());
            std::cerr << "[TID: " << GetThisThreadID() << "] " << e.what() << std::endl;
            std::cerr << "[TID: " << GetThisThreadID() << "] "
                      << "Falling back to CPU algorithm..." << std::endl;
        }
        cpu_func(_forward_args_t(_Args, args));
    }
}

//======================================================================================//