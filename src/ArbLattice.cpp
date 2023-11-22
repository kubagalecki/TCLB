#include "ArbLattice.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <fstream>
#include <numeric>
#include <optional>
#include <unordered_set>

#include "PartitionArbLattice.hpp"
#include "UtilTypes.hpp"
#include "mpitools.hpp"

ArbLattice::ArbLattice(size_t num_snaps_, const UnitEnv& units_, const std::map<std::string, int>& zone_map, const std::string& cxn_path, MPI_Comm comm_) : LatticeBase(ZONESETTINGS, ZONE_MAX, units_), num_snaps(num_snaps_), comm(comm_) {
    readFromCxn(zone_map, cxn_path);
    global_node_dist = computeInitialNodeDist(connect.num_nodes_global, mpitools::MPI_Size(comm));
    partition();
    if (connect.getLocalSize() == 0) throw std::runtime_error{"At least one MPI rank has an empty partition, please use fewer MPI ranks"};  // Realistically, this should never happen
    computeGhostNodes();
    computeLocalPermutation();
    local_bounding_box = getLocalBoundingBox();
}

std::unordered_map<std::string, int> ArbLattice::makeGroupZoneMap(const std::map<std::string, int>& zone_map) const {
    std::unordered_map<std::string, int> retval;
    int group_zone_ind = 0;
    for (const auto& ntf : model->nodetypeflags) retval.emplace(ntf.name, group_zone_ind++);
    for (const auto& [name, zs] : zone_map) retval.emplace("_Z_" + name, group_zone_ind++);
    return retval;
}

void ArbLattice::readFromCxn(const std::map<std::string, int>& zone_map, const std::string& cxn_path) {
    using namespace std::string_literals;
    using namespace std::string_view_literals;

    const auto gz_map = makeGroupZoneMap(zone_map);

    const int comm_rank = mpitools::MPI_Rank(comm), comm_size = mpitools::MPI_Size(comm);

    // Open file + utils for error reporting
    std::fstream file(cxn_path, std::ios_base::in);
    const auto wrap_err_msg = [&](std::string msg) { return "Error while reading "s.append(cxn_path.c_str()).append(" on MPI rank ").append(std::to_string(comm_rank)).append(": ").append(msg); };
    const auto check_file_ok = [&](const std::string& err_message = "Unknown error") {
        if (!file) throw std::ios_base::failure(wrap_err_msg(err_message));
    };
    const auto check_expected_word = [&](std::string_view expected, std::string_view actual) {
        if (expected != actual) {
            const auto err_msg = "Unexpected section header: "s.append(actual).append("; Expected: ").append(expected);
            throw std::logic_error(wrap_err_msg(err_msg));
        }
    };
    check_file_ok("Could not open file");

    // Implement the pattern: 1) read header 2) read size 3) invoke body on the read size, return result
    std::string word;
    const auto process_section = [&](std::string_view header, auto&& body) {
        file >> word;
        check_file_ok("Failed to read section header: "s.append(header));
        check_expected_word(header, word);
        size_t size{};
        file >> size;
        check_file_ok("Failed to read section size: "s.append(header));
        if constexpr (std::is_same_v<void, std::invoke_result_t<decltype(body), size_t>>) {
            body(size);
            check_file_ok("Failed to read section: "s.append(header));
        } else {
            auto retval = body(size);
            check_file_ok("Failed to read section: "s.append(header));
            return retval;
        }
    };

    // Offset directions
    const auto q_provided = process_section("OFFSET_DIRECTIONS", [&file](size_t n_q_provided) {
        std::vector<std::array<int, 3>> retval;
        retval.reserve(n_q_provided);
        for (size_t i = 0; i != n_q_provided; ++i) {
            std::array<int, 3> dirs{};
            file >> dirs[0] >> dirs[1] >> dirs[2];
            retval.push_back(dirs);
        }
        return retval;
    });

    // Check all required dirs are present, construct the lookup table
    std::array<size_t, Q> req_prov_perm{};  // Lookup table between provided and required dirs
    {
        size_t i = 0;
        for (const auto& req : Model_m::offset_directions) {
            const auto prov_it = std::find(q_provided.cbegin(), q_provided.cend(), req);
            if (prov_it == q_provided.cend()) {
                const auto [x, y, z] = req;
                const auto err_msg = "The arbitrary lattice file does not provide the required direction: ["s.append(std::to_string(x)).append(", ").append(std::to_string(y)).append(", ").append(std::to_string(z)).append("]");
                throw std::runtime_error(wrap_err_msg(err_msg));
            }
            req_prov_perm[i++] = std::distance(q_provided.cbegin(), prov_it);
        }
    }

    // Grid size
    file >> word;
    check_file_ok("Failed to read section header: GRID_SIZE");
    check_expected_word("GRID_SIZE", word);
    double grid_size{};
    file >> grid_size;
    check_file_ok("Failed to read section: GRID_SIZE");

    // Groups (and zones) present in the .cxn file
    const auto groups = process_section("NODE_GROUPS", [&file](size_t n_groups) {
        std::vector<std::string> retval(n_groups);
        for (auto& g : retval) file >> g;
        return retval;
    });
    size_t n_groups = groups.size();

    // Check all required groups are present, construct lookup table from groups in the file to groups from the argument
    std::vector<int> group_lookup_table(groups.size());
    std::transform(groups.cbegin(), groups.cend(), group_lookup_table.begin(), [&](const std::string& g) -> int {
        const auto iter = gz_map.find(g);
        if (iter == gz_map.end()) {
            warning("Ignoring unknown group/zone \"%s\"\n", g.c_str());
            --n_groups;
            return -1;
        }
        return iter->second;
    });
    const auto lookup_group = [&group_lookup_table](int id) -> std::optional<int> {
        if (id == -1) return {};
        else
            return {group_lookup_table[id]};
    };
    if (n_groups != gz_map.size()) {
        std::vector<std::string> missing_zones;
        for (const auto& [name, ind] : gz_map)
            if (std::find(groups.cbegin(), groups.cend(), name) == groups.cend()) missing_zones.push_back(name);
        std::string err_msg("The following "s + (missing_zones.size() > 1 ? "groups and/or zones were" : "group/zone was") + " not present in the file: ");
        for (const auto& miss : missing_zones) err_msg.append(miss).append(", ");
        err_msg.pop_back();
        err_msg.pop_back();
        err_msg = wrap_err_msg(err_msg);
        throw std::runtime_error(err_msg);
    }

    // Nodes header
    process_section("NODES", [&](size_t num_nodes_global) {
        // Compute the current rank's offset and number of nodes to read
        const auto chunk_offsets = computeInitialNodeDist(num_nodes_global, static_cast<size_t>(comm_size));
        const auto chunk_begin = static_cast<size_t>(chunk_offsets[comm_rank]), chunk_end = static_cast<size_t>(chunk_offsets[comm_rank + 1]);
        const auto num_nodes_local = chunk_end - chunk_begin;

        connect = ArbLatticeConnectivity(chunk_begin, chunk_end, num_nodes_global, Q);
        connect.grid_size = grid_size;

        // Skip chunk_begin + 1 (header) newlines
        for (size_t i = 0; i != chunk_begin + 1; ++i) file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        check_file_ok("Failed to skip ahead to this rank's chunk");

        // Parse node data
        std::vector<long> nbrs_in_file(q_provided.size());
        for (size_t local_node_ind = 0; local_node_ind != num_nodes_local; ++local_node_ind) {
            // Read coords
            file >> connect.coord(0, local_node_ind) >> connect.coord(1, local_node_ind) >> connect.coord(2, local_node_ind);

            // Read neighbors, map to local (required) ones
            for (auto& nbr : nbrs_in_file) file >> nbr;
            size_t j = 0;
            for (size_t perm : req_prov_perm) connect.neighbor(j++, local_node_ind) = nbrs_in_file[perm];

            // Read and set zones
            auto& n_zones = connect.zones_per_node[local_node_ind];
            file >> n_zones;
            for (size_t z = 0; z != n_zones; ++z) {
                int file_zone;
                file >> file_zone;
                const auto zone = lookup_group(file_zone);
                if (zone) connect.zones.push_back(*zone);
                else
                    --n_zones;
            }

            check_file_ok("Failed to read node data");
        }
    });
}

void ArbLattice::partition() {
    if (mpitools::MPI_Size(comm) == 1) return;

    const auto zero_dir_ind = std::distance(Model_m::offset_directions.cbegin(), std::find(Model_m::offset_directions.cbegin(), Model_m::offset_directions.cend(), OffsetDir{0, 0, 0}));  // Note: the behavior is still correct even if (0,0,0) is not an offset direction
    const auto offset_dir_wgts = std::vector(Model_m::offset_direction_weights.begin(), Model_m::offset_direction_weights.end());
    auto [dist, log] = partitionArbLattice(connect, offset_dir_wgts, zero_dir_ind, comm);
    for (const auto& [type, msg] : log) switch (type) {
            case PartOutput::MsgType::Notice:
                NOTICE(msg.c_str());
                break;
            case PartOutput::MsgType::Warning:
                WARNING(msg.c_str());
                break;
            case PartOutput::MsgType::Error:
                throw std::runtime_error(msg);
        }
    global_node_dist = std::move(dist);
}

void ArbLattice::computeGhostNodes() {
    std::unordered_set<long> ghosts;
    const Span all_nbrs(connect.nbrs.get(), Q * connect.getLocalSize());
    for (auto nbr : all_nbrs)
        if (connect.isGhost(nbr)) ghosts.insert(nbr);
    ghost_nodes.reserve(ghosts.size());
    std::copy(ghosts.cbegin(), ghosts.cend(), std::back_inserter(ghost_nodes));
    std::sort(ghost_nodes.begin(), ghost_nodes.end());
}

void ArbLattice::computeLocalPermutation() {
    local_permutation.resize(connect.getLocalSize());
    std::iota(local_permutation.begin(), local_permutation.end(), 0);
    const auto is_border_node = [&](int lid) {
        for (size_t q = 0; q != Q; ++q) {
            const auto nbr = connect.neighbor(q, lid);
            if (connect.isGhost(nbr)) return true;
        }
        return false;
    };
    const auto interior_begin = std::stable_partition(local_permutation.begin(), local_permutation.end(), is_border_node);
    num_border_nodes = static_cast<size_t>(std::distance(local_permutation.begin(), interior_begin));
    const auto by_zyx = [&](int lid1, int lid2) {  // Sort by z, then y, then x -> this should help with coalesced memory access
        const auto get_zyx = [&](int lid) { return std::array{connect.coord(2, lid), connect.coord(1, lid), connect.coord(0, lid)}; };
        return get_zyx(lid1) < get_zyx(lid2);
    };
    std::sort(local_permutation.begin(), interior_begin, by_zyx);
    std::sort(interior_begin, local_permutation.end(), by_zyx);
}

int ArbLattice::fullLatticePos(double pos) const {
    const auto retval = std::lround(pos / connect.grid_size - .5);
    assert(retval <= std::numeric_limits<int>::max() && retval >= std::numeric_limits<int>::min());
    return static_cast<int>(retval);
}

lbRegion ArbLattice::getLocalBoundingBox() const {
    const auto x = Span(connect.coords.get(), connect.getLocalSize()), y = Span(std::next(connect.coords.get(), connect.getLocalSize()), connect.getLocalSize()), z = Span(std::next(connect.coords.get(), 2 * connect.getLocalSize()), connect.getLocalSize());
    const auto [minx_it, maxx_it] = std::minmax_element(x.begin(), x.end());
    const auto [miny_it, maxy_it] = std::minmax_element(y.begin(), y.end());
    const auto [minz_it, maxz_it] = std::minmax_element(z.begin(), z.end());
    const double x_min = *minx_it, x_max = *maxx_it, y_min = *miny_it, y_max = *maxy_it, z_min = *minz_it, z_max = *maxz_it;
    return lbRegion(x_min, y_min, z_min, x_max - x_min, y_max - y_min, z_max - z_min);
}
