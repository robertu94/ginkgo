/*******************************<GINKGO LICENSE>******************************
Copyright (c) 2017-2020, the Ginkgo authors
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
******************************<GINKGO LICENSE>*******************************/

#ifndef GKO_PUBLIC_CORE_BASE_MACHINE_TOPOLOGY_HPP_
#define GKO_PUBLIC_CORE_BASE_MACHINE_TOPOLOGY_HPP_


#include <cassert>
#include <cstddef>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>


#include <ginkgo/config.hpp>
#include <ginkgo/core/base/exception.hpp>
#include <ginkgo/core/base/exception_helpers.hpp>


#if GKO_HAVE_HWLOC

#include <hwloc.h>

#else

struct hwloc_obj_type_t {};
struct hwloc_obj_t {};

#endif


struct hwloc_topology;


namespace gko {


class MachineTopology;


/**
 * This global function initializes the topology in its first call and stores
 * the MachineTopology global object in a shared_ptr that any gko function/class
 * can query.
 *
 * This makes sure that that the topology object is not re-initialized every
 * time it is needed and as the topology is is relevant only for the machine,
 * each re-initialization populates the exact same topology tree.
 *
 * @return  the global MachineTopology object.
 */
extern const MachineTopology *get_machine_topology();


/**
 * The machine topology class represents the heirarchical topology of a machine,
 * including NUMA nodes, cores and PCI Devices. Various infomation of the
 * machine are gathered with the help of the Hardware Locality library (hwloc).
 *
 * This class also provides functionalities to bind objects in the topology to
 * the execution objects. Binding can enhance performance by allowing data to be
 * closer to the executing object.
 *
 * See the hwloc documentation
 * (https://www.open-mpi.org/projects/hwloc/doc/v2.4.0/) for a more detailed
 * documentation on topology detection and binding interfaces.
 *
 * @note A global object of MachineTopology type is created when first needed
 * and only destroyed at the end of the program. This means that any subsequent
 * queries will be from the same global object and hence atomic on
 * multi-threaded queries.
 */
class MachineTopology {
    friend const MachineTopology *get_machine_topology();

private:
    /**
     * This struct holds the attributes for a normal non-IO object.
     */
    struct normal_obj_info {
        /**
         * The hwloc object.
         */
        hwloc_obj_t obj;

        /**
         * The numa number of the object.
         */
        int numa;

        /**
         * The logical_id assigned by the OS.
         */
        size_type logical_id;

        /**
         * The physical_id assigned to the object.
         */
        size_type physical_id;

        /**
         * The global persistent id assigned to the object by hwloc.
         */
        size_type gp_id;

        /**
         * The memory size of the object.
         */
        size_type memory_size;
    };


    /**
     * This struct holds the attributes for an IO/Misc object.
     *
     * Mainly used for PCI devices. The identifier important for PCI devices is
     * the PCI Bus ID, stored here as a string. PCI devices themselves usually
     * contain Hard disks, network components as well as other objects that are
     * not important for our case.
     *
     * In many cases, hwloc is able to identify the OS devices that belong to a
     * certain PCI Bus ID and here they are stored in the io children vector. A
     * list of their names are also additionally stored for easy access and
     * comparison.
     *
     * @note IO children can have names such as ibX for Infiniband cards, cudaX
     * for NVIDIA cards with CUDA and rsmiX for AMD cards.
     */
    struct io_obj_info {
        /**
         * The hwloc object.
         */
        hwloc_obj_t obj;

        /**
         * The logical_id assigned by the OS.
         */
        size_type logical_id;

        /**
         * The physical_id assigned to the object.
         */
        size_type physical_id;

        /**
         * The global persistent id assigned to the object by hwloc.
         */
        size_type gp_id;

        /**
         * The non-io parent object.
         */
        hwloc_obj_t non_io_ancestor;

        /**
         * The closest numa.
         */
        int numa;

        /**
         * The io children objects (usually software OS devices).
         */
        std::vector<hwloc_obj_t> io_children;

        /**
         * The names of the io children objects.
         */
        std::vector<std::string> io_children_name;

        /**
         * The PCI Bus ID
         */
        std::string pci_busid;
    };

public:
    /**
     * Do not allow the MachineTopology object to be copied/moved. There should
     * be only one global object per execution.
     */
    MachineTopology(MachineTopology &) = delete;
    MachineTopology(MachineTopology &&) = delete;
    MachineTopology &operator=(MachineTopology &) = delete;
    MachineTopology &operator=(MachineTopology &&) = delete;
    ~MachineTopology() = default;

    /**
     * Bind the object associated with the id to a core.
     *
     * @param id  The id of the object to be bound.
     */
    void bind_to_core(size_type id) { hwloc_binding_helper(this->cores_, id); }

    /**
     * Bind the object associated with the id to a Processing unit(PU).
     *
     * @param id  The id of the object to be bound.
     */
    void bind_to_pu(size_type id) { hwloc_binding_helper(this->pus_, id); }

    /**
     * Get the object of type PU associated with the id.
     *
     * @param id  The id of the PU
     * @return  the PU object struct.
     */
    const normal_obj_info &get_pu(size_type id)
    {
        GKO_ENSURE_IN_BOUNDS(id, this->pus_.size());
        return this->pus_[id];
    }

    /**
     * Get the object of type core associated with the id.
     *
     * @param id  The id of the core
     * @return  the core object struct.
     */
    const normal_obj_info &get_core(size_type id)
    {
        GKO_ENSURE_IN_BOUNDS(id, this->cores_.size());
        return this->cores_[id];
    }

    /**
     * Get the object of type pci device associated with the id.
     *
     * @param id  The id of the pci device
     * @return  the PCI object struct.
     */
    const io_obj_info &get_pci_device(size_type id)
    {
        GKO_ENSURE_IN_BOUNDS(id, this->pci_devices_.size());
        return this->pci_devices_[id];
    }

    /**
     * Get the number of PU objects stored in this Topology tree.
     *
     * @return  the number of PUs.
     */
    size_type get_num_pus() const { return this->pus_.size(); }

    /**
     * Get the number of core objects stored in this Topology tree.
     *
     * @return  the number of cores.
     */
    size_type get_num_cores() const { return this->cores_.size(); }

    /**
     * Get the number of PCI device objects stored in this Topology tree.
     *
     * @return  the number of PCI devices.
     */
    size_type get_num_pci_devices() const { return this->pci_devices_.size(); }

    /**
     * Get the number of NUMA objects stored in this Topology tree.
     *
     * @return  the number of NUMA objects.
     */
    size_type get_num_numas() const { return this->num_numas_; }

protected:
    /**
     * Creates a new MachineTopology object.
     */
    static std::shared_ptr<MachineTopology> create()
    {
        return std::shared_ptr<MachineTopology>(new MachineTopology());
    }

    /**
     * The default constructor that loads the different objects.
     */
    MachineTopology();

    /**
     * @internal
     *
     * A helper function that binds the object with an id.
     */
    template <typename ObjInfoType>
    void hwloc_binding_helper(std::vector<ObjInfoType> &obj, size_type id);

    /**
     * @internal
     *
     * A helper function that prints the topology tree of an object to a given
     * depth. Provided from the hwloc library.
     */
    static void hwloc_print_children(hwloc_topology *topology, hwloc_obj_t obj,
                                     int depth);

    /**
     * @internal
     *
     * Load the objects of a certain HWLOC type.
     *
     * @note The objects should be sorted by logical index since hwloc uses
     * logical index with these functions
     */
    void load_objects(hwloc_obj_type_t type,
                      std::vector<normal_obj_info> &vector);

    /**
     * @internal
     *
     * Load the objects of a certain HWLOC type.
     *
     * @note The objects should be sorted by logical index since hwloc uses
     * logical index with these functions
     */
    void load_objects(hwloc_obj_type_t type, std::vector<io_obj_info> &vector);

    /**
     * @internal
     *
     * Initialize the topology object.
     */
    hwloc_topology *init_topology();


private:
    std::vector<normal_obj_info> pus_;
    std::vector<normal_obj_info> cores_;
    std::vector<io_obj_info> pci_devices_;
    size_type num_numas_;

    template <typename T>
    using topo_manager = std::unique_ptr<T, std::function<void(T *)>>;
    topo_manager<hwloc_topology> topo_;
};


}  // namespace gko


#endif  // GKO_PUBLIC_CORE_BASE_MACHINE_TOPOLOGY_HPP_
