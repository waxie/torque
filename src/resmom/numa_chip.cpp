#include <iostream>
#include <string>
#include <vector>
#include <errno.h>
#include "pbs_config.h"

#ifdef PENABLE_LINUX_CGROUPS
#include "machine.hpp"
#include <hwloc.h>

#ifdef MIC
#include <hwloc/intel-mic.h>
#endif

#ifdef NVIDIA_GPUS
#ifdef NVML_API
#include <hwloc/nvml.h>
#endif
#endif

#include "pbs_error.h"
#include "log.h"
#include "mom_server_lib.h" /* log_nvml_error */

using namespace std;

#define INTEL 1
#define AMD   2


  Chip::Chip() : totalThreads(0), totalCores(0), id(0), availableThreads(0), availableCores(0),
                 chip_exclusive(false)
    {
    memset(chip_cpuset_string, 0, MAX_CPUSET_SIZE);
    memset(chip_nodeset_string, 0, MAX_NODESET_SIZE);
    }

  Chip::~Chip()
    {
    id = -1;
    }

  /* initializeNonNUMAChip will initialize an instance of a NUMA Chip and then populate
     it with all of the cores and threads in the system as if there were a NUMA Chip */
  int Chip::initializeNonNUMAChip(hwloc_obj_t socket_obj, hwloc_topology_t topology)
    {
    hwloc_obj_t core_obj;
    hwloc_obj_t prev = NULL;
    int rc;

    rc = get_machine_total_memory(topology, &this->memory);
    if (rc != 0)
      {
      log_err(-1, __func__, "could not get memory for NonNUMAChip");
      /* Not fatal for now. Go on */
      }
    else
      {
      // Store the memory in kb
      this->memory = this->memory / 1024;
      }

    while ((core_obj = hwloc_get_next_obj_by_type(topology, HWLOC_OBJ_CORE, prev)) != NULL)
      {
      Core newCore;

      newCore.initializeCore(core_obj, topology);
      this->cores.push_back(newCore);
      this->totalThreads += newCore.getNumberOfProcessingUnits();
      prev = core_obj;
      }

    this->totalCores = this->cores.size();
    this->availableCores = this->totalCores;
    this->availableThreads = this->totalThreads;
    this->chip_cpuset = hwloc_topology_get_allowed_cpuset(topology);
    this->chip_nodeset = hwloc_topology_get_allowed_nodeset(topology);
    hwloc_bitmap_list_snprintf(chip_cpuset_string, MAX_CPUSET_SIZE, this->chip_cpuset);
    hwloc_bitmap_list_snprintf(chip_nodeset_string, MAX_CPUSET_SIZE, this->chip_nodeset);

    if (this->totalCores == this->totalThreads)
      {
      this->isThreaded = false;
      }
    else
      {
      this->isThreaded = true;
      }
   
    this->initializePCIDevices(NULL, topology);
    
    return(PBSE_NONE);
    }

  int Chip::initializeChip(hwloc_obj_t chip_obj, hwloc_topology_t topology)
    {
    hwloc_obj_t core_obj;
    hwloc_obj_t prev;

    this->id = chip_obj->logical_index;
    // Store the memory in kb
    this->memory = chip_obj->memory.local_memory / 1024;
    this->available_memory = this->memory;
    this->chip_cpuset = chip_obj->allowed_cpuset;
    this->chip_nodeset = chip_obj->allowed_nodeset;
    hwloc_bitmap_list_snprintf(this->chip_cpuset_string, MAX_CPUSET_SIZE, this->chip_cpuset);
    hwloc_bitmap_list_snprintf(this->chip_nodeset_string, MAX_CPUSET_SIZE, this->chip_nodeset);
    this->totalCores = hwloc_get_nbobjs_inside_cpuset_by_type(topology, this->chip_cpuset, HWLOC_OBJ_CORE);
    this->totalThreads = hwloc_get_nbobjs_inside_cpuset_by_type(topology, this->chip_cpuset, HWLOC_OBJ_PU);
    this->availableCores = this->totalCores;
    this->availableThreads = this->totalThreads;

    if (this->totalCores == this->totalThreads)
      {
      this->isThreaded = false;
      }
    else
      {
      this->isThreaded = true;
      }

    /* Find all the cores that belong to this numa chip */
    prev = NULL;
    while ((core_obj = hwloc_get_next_obj_by_type(topology, HWLOC_OBJ_CORE, prev)) != NULL)
      {
      int is_in_subtree;

      is_in_subtree = hwloc_obj_is_in_subtree(topology, core_obj, chip_obj);
      if (is_in_subtree)
        {
        Core new_core;

        new_core.initializeCore(core_obj, topology);
        this->cores.push_back(new_core);
        }
      prev = core_obj;
      }

   this->initializePCIDevices(chip_obj, topology);

   return(PBSE_NONE);
    }

  int Chip::getTotalCores()
    {
    return(this->totalCores);
    }

  int Chip::getTotalThreads()
    {
    return(this->totalThreads);
    }

  int Chip::getAvailableCores()
    {
    return(this->availableCores);
    }

  int Chip::getAvailableThreads()
    {
    return(this->availableThreads);
    }

  int Chip::getMemory()
    {
    return(this->memory);
    }

  bool Chip::chipIsAvailable() const
    {
    if ((this->availableThreads == this->totalThreads) &&
        (this->availableCores == this->totalCores))
      return(true);

    return(false);
    }

#ifdef NVIDIA_GPUS
  #ifdef NVML_API
  int Chip::initializeNVIDIADevices(hwloc_obj_t chip_obj, hwloc_topology_t topology)
    {
    nvmlReturn_t rc;

    /* Initialize the NVML handle. 
     *
     * nvmlInit should be called once before invoking any other methods in the NVML library. 
     * A reference count of the number of initializations is maintained. Shutdown only occurs 
     * when the reference count reaches zero.
     * */
    rc = nvmlInit();
    if (rc != NVML_SUCCESS && rc != NVML_ERROR_ALREADY_INITIALIZED)
      {
      log_nvml_error(rc, NULL, __func__);
      return(PBSE_NONE);
      }

    unsigned int device_count = 0;

    /* Get the device count. */
    rc = nvmlDeviceGetCount(&device_count);
    if (rc == NVML_SUCCESS)
      {
      nvmlDevice_t gpu;

      /* Get the nvml device handle at each index */
      for (unsigned int idx = 0; idx < device_count; idx++)
        {
        rc = nvmlDeviceGetHandleByIndex(idx, &gpu);
  
        if (rc != NVML_SUCCESS)
          {
          /* TODO: get gpuid from nvmlDevice_t struct */
          log_nvml_error(rc, NULL, __func__);
          }

        /* Use the hwloc library to determine device locality */
        hwloc_obj_t gpu_obj;
        hwloc_obj_t ancestor_obj;
        int is_in_tree;
    
        gpu_obj = hwloc_nvml_get_device_osdev(topology, gpu);
        if (gpu_obj == NULL)
          continue;
    
        if (chip_obj == NULL) 
          { // this came from the Non NUMA
          PCI_Device new_device;
    
          new_device.initializePCIDevice(gpu_obj, idx, topology);
          this->devices.push_back(new_device);
          }
        else
          {
          /* TODO: test this block of code on a NUMA-capable server with the k40/k80 installed */
          PCI_Device new_device;
          ancestor_obj = hwloc_get_ancestor_obj_by_type(topology, HWLOC_OBJ_NODE, gpu_obj);
          if (ancestor_obj != NULL)
            {
            if (ancestor_obj->logical_index == chip_obj->logical_index)
              {
              PCI_Device new_device;
    
              new_device.initializePCIDevice(gpu_obj, idx, topology);
              this->devices.push_back(new_device);
              }
            }
          }
        }
      }
    else
      {
      log_nvml_error(rc, NULL, __func__);
      }

    /* Shutdown the NVML handle. 
     *
     * nvmlShutdown should be called after NVML work is done, once for each call to nvmlInit() 
     * A reference count of the number of initializations is maintained. Shutdown only occurs when 
     * the reference count reaches zero. For backwards compatibility, no error is reported if 
     * nvmlShutdown() is called more times than nvmlInit().
     * */
    rc = nvmlShutdown();
    if (rc != NVML_SUCCESS)
      {
      log_nvml_error(rc, NULL, __func__);
      }

    return(PBSE_NONE);
    }
  #endif
#endif

#ifdef MIC
  int Chip::initializeMICDevices(hwloc_obj_t chip_obj, hwloc_topology_t topology)
    {
    /* Get any Intel MIC devices */
    /* start at indes 0 and go until hwloc_intel_mic_get_device_osdev_by_index
       returns NULL */
    for (int idx = 0; ; idx++)
      {
      hwloc_obj_t mic_obj;
      hwloc_obj_t ancestor_obj;
      int is_in_tree;

      mic_obj = hwloc_intel_mic_get_device_osdev_by_index(topology, idx);
      if (mic_obj == NULL)
        break;

      if (chip_obj == NULL) 
        { /* this came from the Non NUMA */
        PCI_Device new_device;

        new_device.initializePCIDevice(mic_obj, idx, topology);
        this->devices.push_back(new_device);
        }
      else
        {
        PCI_Device new_device;
        ancestor_obj = hwloc_get_ancestor_obj_by_type(topology, HWLOC_OBJ_NODE, mic_obj);
        if (ancestor_obj != NULL)
          {
          if (ancestor_obj->logical_index == chip_obj->logical_index)
            {
            PCI_Device new_device;

            new_device.initializePCIDevice(mic_obj, idx, topology);
            this->devices.push_back(new_device);
            }
          }
        }
      }
    return(PBSE_NONE);
    }
#endif


  int Chip::initializePCIDevices(hwloc_obj_t chip_obj, hwloc_topology_t topology)
    {
    /* See if there are any accelerators */

#ifdef MIC
    this->initializeMICDevices(chip_obj, topology);
#endif

#ifdef NVIDIA_GPUS
  #ifdef NVML_API
    this->initializeNVIDIADevices(chip_obj, topology);
  #endif
#endif
    return(PBSE_NONE);

    }

  void Chip::displayAsString(

    stringstream &out) const

    {
    out << "    Chip " << this->id << " (" << this->memory << "KB)\n";

    for (unsigned int i = 0; i < this->cores.size(); i++)
      this->cores[i].displayAsString(out);
    
    for (unsigned int i = 0; i < this->devices.size(); i++)
      this->devices[i].displayAsString(out);
    } // END displayAsString() 



  void Chip::setMemory(

    hwloc_uint64_t memory)

    {
    this->memory = memory;
    this->available_memory = memory;
    }

  void Chip::setCores(

    int cores)

    {
    this->totalCores = cores;
    this->availableCores = cores;
    }

  void Chip::setThreads(

    int threads)

    {
    this->totalThreads = threads;
    this->availableThreads = threads;
    }

  void Chip::setChipAvailable(

    bool available)

    {
    this->chip_exclusive = !available;
    }
  
  void Chip::setId(

    int id)

    {
    this->id = id;
    }

  // This function is for unit testing
  void Chip::make_core(
      
    int id)

    {
    Core c;
    c.id = id;
    c.totalThreads = 2;
    c.free = true;
    c.indices.push_back(id);
    c.indices.push_back(id + 12);
    c.is_index_busy.push_back(false);
    c.is_index_busy.push_back(false);
    c.processing_units_open = 2;
    this->cores.push_back(c);
    }
  


  /*
   * how_many_tasks_fit()
   *
   * Determines how many tasks from req r fit on this chip
   * @param r - the req we're examining
   * @return the number of tasks that fit. This can be 0
   */

  int Chip::how_many_tasks_fit(

    const req &r)

    {
    int cpu_tasks;
    int mem_tasks = 0;

    if (this->chip_exclusive == false)
      {
      if (r.getPlacementType() == use_cores)
        cpu_tasks = this->availableCores / r.getExecutionSlots();
      else
        cpu_tasks = this->availableThreads / r.getExecutionSlots();

      long long memory = r.getMemory();

      // Memory isn't required for submission
      if (memory == 0)
        return(cpu_tasks);

      mem_tasks = this->available_memory / memory;

      // return the lower of the two values
      if (mem_tasks > cpu_tasks)
        mem_tasks = cpu_tasks;
      }
      
    return(mem_tasks);
    } // END how_many_tasks_fit()



  /*
   * place_task_by_cores()
   *
   * places the task, knowing that we must use only cores
   *
   * @param execution_slots_per_task - the number of cores to place for this task
   * @param a - the allocation we're marking these used for
   */

  void Chip::place_task_by_cores(

    int         execution_slots_per_task,
    allocation &a)

    {
    // Get the core indices we will use
    unsigned int j = 0;
    for (int i = 0; i < execution_slots_per_task; i++)
      {
      while (j < this->cores.size())
        {
        if (this->cores[j].free == true)
          {
          this->cores[j].mark_as_busy(this->cores[j].id);
          a.cpu_indices.push_back(this->cores[j].id);
          a.cores++;
          this->availableCores--;
          this->availableThreads -= this->cores[j].totalThreads;
          a.threads += this->cores[j].totalThreads;
          j++;

          break;
          }
        else
          j++;
        }
      }
    } // END place_task_by_cores()



  /*
   * place_task_by_threads()
   *
   * places the task, knowing that we can use threads
   *
   * @param execution_slots_per_task - the number of cores to place for this task
   * @param a - the allocation we're marking these used for
   */

  void Chip::place_task_by_threads(

    int         execution_slots_per_task,
    allocation &a)

    {
    // Place for being able to use threads
    int slots_left = execution_slots_per_task;
    // Get the core indices we will use
    for (unsigned int j = 0; j < this->cores.size() && slots_left > 0; j++)
      {
      int index;

      if (this->cores[j].free == true)
        {
        this->availableCores--;
        a.cores++;
        }

      while (((index = this->cores[j].get_open_processing_unit()) != -1) &&
             (slots_left > 0))
        {
        this->availableThreads--;
        slots_left--;
        a.threads++;
        a.cpu_indices.push_back(index);
        }
      }
    } // END place_task_by_threads()



  /*
   * place_task()
   *
   * Places as many tasks up to to_place from req r on this chip as are available.
   * @param jobid - the jobid whose task's we're placing
   * @param r - the req from which we're placing tasks
   * @param master - the master allocation for the socket this chip belongs to
   * @param to_place the maximum number of tasks to place
   * @return the number of tasks placed
   */

  int Chip::place_task(

    const char *jobid,
    const req  &r,
    allocation &master,
    int         to_place)

    {
    allocation     a(jobid);
    int            tasks_placed = 0;
    int            execution_slots_per_task = r.getExecutionSlots();
    hwloc_uint64_t mem_per_task = r.getMemory();

    if (this->chip_exclusive == false)
      {
      if (r.getThreadUsageString() == use_cores)
        a.cores_only = true;
      else
        a.cores_only = false;

      for (; tasks_placed < to_place; tasks_placed++)
        {
        // Stop if we can't fit any more tasks on this chip
        if (this->available_memory < mem_per_task)
          break;

        if (a.cores_only == true)
          {
          if (this->availableCores < execution_slots_per_task)
            break;
          }
        else
          {
          if (this->availableThreads < execution_slots_per_task)
            break;
          }

        this->available_memory -= mem_per_task;
        a.memory += mem_per_task;
        a.cpus += execution_slots_per_task;
        
        if (a.cores_only == true)
          place_task_by_cores(execution_slots_per_task, a);
        else
          place_task_by_threads(execution_slots_per_task, a);
        }

      if (master.place_type == exclusive_chip)
        this->chip_exclusive = true;
      } // if chip_exclusive == false

    if (tasks_placed > 0)
      {
      // Add this as a memory node
      a.mem_indices.push_back(this->id);
      this->allocations.push_back(a);
      master.add_allocation(a);
      }

    return(tasks_placed);
    } // END place_task()



  /*
   * free_cpu_index()
   *
   * Marks processing unit at index as available
   * @param index - the os index of the processing unit to mark as unused
   */

  void Chip::free_cpu_index(

    int index)

    {
    bool core_is_now_free = false;
    for (unsigned int i = 0; i < this->cores.size(); i++)
      {
      if (this->cores[i].free == true)
        continue;

      if (this->cores[i].free_pu_index(index, core_is_now_free) == true)
        {
        if (core_is_now_free == true)
          {
          this->availableCores++;
          }

        return;
        }
      }

    } // END free_cpu_index()



  /*
   * free_task()
   *
   * Frees all of the cores that are in use for the job that matches jobid
   * @param jobid - the id of the job in question
   * @return true if this chip is completely free
   */

  bool Chip::free_task(

    const char *jobid)

    {
    int  to_remove = -1;
    bool totally_free = false;

    for (unsigned int i = 0; i < this->allocations.size(); i++)
      {
      if (!strcmp(this->allocations[i].jobid, jobid))
        {
        to_remove = i;
        this->availableThreads += this->allocations[i].threads;
        this->available_memory += this->allocations[i].memory;

        // Now mark the individual cores as available
        for (unsigned int j = 0; j < this->allocations[i].cpu_indices.size(); j++)
          free_cpu_index(this->allocations[i].cpu_indices[j]);
        
        break;
        }
      }

    if (to_remove != -1)
      this->allocations.erase(this->allocations.begin() + to_remove);

    if ((this->availableThreads == this->totalThreads) &&
        (this->availableCores == this->totalCores))
      {
      this->chip_exclusive = false;
      totally_free = true;
      }

    return(totally_free);
    } // END free_task()

#endif /* PENABLE_LINUX_CGROUPS */  