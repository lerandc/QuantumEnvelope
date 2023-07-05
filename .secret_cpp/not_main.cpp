#include <chrono>
#include <iostream>
#include <random>
#include <sul/dynamic_bitset.hpp>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <array>

typedef sul::dynamic_bitset<> spin_det_t;

template<>
struct std::hash<spin_det_t> {
  std::size_t operator()(spin_det_t const& s) const noexcept {
    return std::hash<std::string>()(s.to_string());
  }
};

enum spin_type_e {
  ALPHA = 0,
  BETA  = 1,
  SPIN_MAX,
};

typedef std::array<spin_det_t, 2> det_t;
template<>
struct std::hash<det_t> {
  std::size_t operator()(det_t const& s) const noexcept {
    std::size_t h1 = std::hash<spin_det_t>{}(s[0]);
    std::size_t h2 = std::hash<spin_det_t>{}(s[1]);
    return h1 ^ (h2 << 1);
  }
};
std::ostream& operator<<(std::ostream& os, const det_t& obj) {
  return os << "(" << obj[0] << "," << obj[1] << ")";
}

typedef sul::dynamic_bitset<> spin_occupancy_mask_t;
typedef std::array<spin_occupancy_mask_t, 2> occupancy_mask_t;

typedef sul::dynamic_bitset<> spin_unoccupancy_mask_t;
typedef std::array<spin_unoccupancy_mask_t, 2> unoccupancy_mask_t;

typedef std::array<uint64_t, 4> eri_4idx_t;


spin_det_t get_phase_mask(spin_det_t p) {
  size_t i = 0;
  while(true) {
    spin_det_t q = (p << (1 << i++));
    if(!q.any()) return p;
    p ^= q;
  }
  /*
  for(size_t i = 0; (p << (1 << i)).any(); i++)
     { p ^= (p << (1 << i)); }
  return p;
*/
}

int get_phase_single(spin_det_t d, size_t h, size_t p) {
  auto pm     = get_phase_mask(d);
  bool parity = (pm[h] ^ pm[p]);
  //auto tmp = std::pow(-1, parity);
  //return (p> h) ? tmp : -tmp;
  return (parity ^ (p > h)) ? 1 : -1;
}

uint64_t binom(int n, int k) {
  if(k == 0 || k == n) return 1;
  return binom(n - 1, k - 1) + binom(n - 1, k);
}

// We don't need it.
// But one day... Map a spin_det_t to a int
uint64_t unchoose(int n, spin_det_t S) {
  auto k = S.count();
  if((k == 0) || (k == n)) return 0;
  auto j = S.find_first();
  if(k == 1) return j;
  S >>= 1;
  if(!j) return unchoose(n - 1, S);
  return binom(n - 1, k - 1) + unchoose(n - 1, S);
}

//// i-th lexicographical bit string of lenth n with popcount k
spin_det_t combi(size_t i, size_t n, size_t k, size_t N = 0) {
  if(N == 0) N = n;
  if(k == 0) return spin_det_t(N);

  assert(i < binom(n, k));
  auto n0 = binom(n - 1, k - 1);
  if(i < n0) {
    return spin_det_t(N, 1) | (combi(i, n - 1, k - 1, N) << 1);
  } else {
    return combi(i - n0, n - 1, k, N) << 1;
  }
}

template<typename T>
spin_det_t vec_to_spin_det(std::vector<T> idx, size_t n_orb) {
  spin_det_t res(n_orb);
  for(auto i : idx) res[i] = 1;
  return res;
}

std::vector<unsigned> get_dets_index_statisfing_masks(std::vector<det_t>& psi,
                                                      occupancy_mask_t occupancy_mask,
                                                      unoccupancy_mask_t unoccupancy_mask) {
  std::vector<unsigned> matching_indexes;

  const auto& [alpha_omask, beta_omask] = occupancy_mask;
  const auto& [alpha_umask, beta_umask] = unoccupancy_mask;
  for(unsigned i = 0; i < psi.size(); i++) {
    const auto& [det_alpha, det_beta] = psi[i];
    bool cond_occupancy = alpha_omask.is_subset_of(det_alpha) && beta_omask.is_subset_of(det_beta);
    bool cond_unoccupancy =
        alpha_umask.is_subset_of(~det_alpha) && beta_umask.is_subset_of(~det_beta);
    if(cond_occupancy && cond_unoccupancy) matching_indexes.push_back(i);
  }
  return matching_indexes;
}

spin_det_t apply_single_excitation(spin_det_t s, uint64_t hole, uint64_t particle) {
  assert(s[hole] == 1);
  assert(s[particle] == 0);

  auto s2      = spin_det_t{s};
  s2[hole]     = 0;
  s2[particle] = 1;
  return s2;
}

det_t apply_single_spin_excitation(det_t s, spin_type_e spin, uint64_t hole, uint64_t particle) {
  auto opp_spin = (spin + 1) % 2;

  assert(s[spin][hole] == 1);
  assert(s[spin][particle] == 0);

  auto s2            = det_t{s};
  s2[spin][hole]     = 0;
  s2[spin][particle] = 1;
  return s2;
}

enum integrals_categorie_e { IC_A, IC_B, IC_C, IC_D, IC_E, IC_F, IC_G };

/*
for real orbitals, return same 4-tuple for all equivalent integrals
    returned (i,j,k,l) should satisfy the following:
        i <= k
        j <= l
        (k < l) or (k==l and i <= j)
*/

integrals_categorie_e integral_category(eri_4idx_t idx) {
  const auto& [i, j, k, l] = idx;
  if((i == l)) return IC_A;
  if((i == k) && (j == l)) return IC_B;
  if((i == k) || (j == l)) {
    if(j != k) return IC_C;
    return IC_D;
  }
  if(j == k) return IC_E;
  if((i == j) && (k == l)) return IC_F;
  if((i == j) || (k == l)) return IC_E;
  return IC_G;
}

typedef uint64_t det_idx_t;
typedef float phase_t;
typedef std::pair<std::pair<det_idx_t, det_idx_t>, phase_t> H_contribution_t;

std::vector<H_contribution_t> category_A(uint64_t N_orb, eri_4idx_t idx, std::vector<det_t>& psi) {
  const auto& [i, j, k, l] = idx;

  auto occ = spin_occupancy_mask_t(N_orb);
  occ[i]   = 1;

  auto unocc = spin_unoccupancy_mask_t(N_orb);
  std::vector<H_contribution_t> result;
  // Phase is always one
  for(auto index : get_dets_index_statisfing_masks(psi, {occ, occ}, {unocc, unocc})) {
    result.push_back({{index, index}, 1});
  }
  return result;
}

std::vector<H_contribution_t> category_B(uint64_t N_orb, eri_4idx_t idx, std::vector<det_t>& psi) {
  const auto& [i, j, k, l] = idx;

  const auto occ    = spin_occupancy_mask_t(N_orb);
  auto occ_i        = spin_occupancy_mask_t(N_orb);
  occ_i[i]          = 1;
  auto occ_j        = spin_occupancy_mask_t(N_orb);
  occ_j[j]          = 1;
  const auto occ_ij = occ_i | occ_j;
  const auto unocc  = spin_unoccupancy_mask_t(N_orb);

  // Phase is always one
  std::vector<H_contribution_t> result;
  for(auto index : get_dets_index_statisfing_masks(psi, {occ, occ_ij}, {unocc, unocc}))
    result.push_back({{index, index}, 1});
  for(auto index : get_dets_index_statisfing_masks(psi, {occ_ij, occ}, {unocc, unocc}))
    result.push_back({{index, index}, 1});
  for(auto index : get_dets_index_statisfing_masks(psi, {occ_i, occ_j}, {unocc, unocc}))
    result.push_back({{index, index}, 1});
  for(auto index : get_dets_index_statisfing_masks(psi, {occ_j, occ_i}, {unocc, unocc}))
    result.push_back({{index, index}, 1});
  return result;
}

void category_C_ijil(uint64_t N_orb, eri_4idx_t idx, std::vector<det_t>& psi,
                     std::vector<H_contribution_t>& result) {
  const auto& [i, j, k, l] = idx;
  assert(i == k);
  uint64_t a, b, c;
  a = i;
  b = j;
  c = l;

  for(size_t spin_a = 0; spin_a < SPIN_MAX; spin_a++) {
    for(size_t spin_bc = 0; spin_bc < SPIN_MAX; spin_bc++) {
      auto occ_alpha           = spin_occupancy_mask_t(N_orb);
      auto occ_beta            = spin_occupancy_mask_t(N_orb);
      auto unocc_alpha         = spin_unoccupancy_mask_t(N_orb);
      auto unocc_beta          = spin_unoccupancy_mask_t(N_orb);
      occupancy_mask_t occ     = {occ_alpha, occ_beta};
      unoccupancy_mask_t unocc = {unocc_alpha, unocc_beta};

      occ[spin_a][a]    = 1;
      occ[spin_bc][b]   = 1;
      unocc[spin_bc][c] = 1;
      for(auto index0 : get_dets_index_statisfing_masks(psi, occ, unocc)) {
        const auto det0 = psi[index0];
        const auto det1 =
            apply_single_spin_excitation(det0, static_cast<spin_type_e>(spin_bc), b, c);

        for(uint64_t index1 = 0; index1 < psi.size(); index1++) {
          if(psi[index1] == det1) {
            result.push_back({{index0, index1}, get_phase_single(det0[spin_bc], b, c)});
          }
        }
      }
    }
  }
}

std::vector<H_contribution_t> category_C(uint64_t N_orb, eri_4idx_t idx, std::vector<det_t>& psi) {
  std::vector<H_contribution_t> result;

  const auto& [i, j, k, l] = idx;
  uint64_t a, b, c;

  if(i == k) {
    a = i;
    b = j;
    c = l;
  } else {
    a = j;
    b = i;
    c = k;
  }
  const eri_4idx_t idx_bc = {a, b, a, c};
  const eri_4idx_t idx_cb = {a, c, a, b};

  category_C_ijil(N_orb, idx_bc, psi, result);
  category_C_ijil(N_orb, idx_cb, psi, result);
  return result;
}

int main(int argc, char** argv) {
  int Norb  = std::stoi(argv[1]);
  int Nelec = 4;
  int Ndet  = std::min(10, (int)binom(Norb, Nelec));
  std::vector<det_t> psi;
  for(int i = 0; i < Ndet; i++) {
    auto d1 = combi(i, Norb, Nelec);
    for(int j = 0; j < Ndet; j++) {
      auto d2 = combi(j, Norb, Nelec);
      psi.push_back({d1, d2});
    }
  }
  /*
  // test combi/unchoose
  for(int i = 0; i < binom(Norb, Nelec); i++) {
    assert(unchoose(Norb, combi(i, Norb, Nelec)) == i);
    std::cout << i << " " << combi(i, Norb, Nelec) << std::endl;
  }
*/
  /*
  auto res1 = category_C(Norb, {1, 2, 1, 4}, psi);
  for(size_t i = 0; i < res1.size(); i++) {
    auto& [pd1, ph1] = res1[i];
    std::cout << i << "\t: " << " " << psi[pd1.first] << "->"  << psi[pd1.second] << "\t" << ph1
              << std::endl;
  }
  return 0;
*/

  spin_det_t d1{"11000"}; // 4->2
  spin_det_t d2{"10001"}; // 4->2
  spin_det_t d3{"01100"}; // 2->4
  spin_det_t d4{"00101"}; // 2->4
  std::cout << "d" << d1 << " " << get_phase_single(d1, 4, 2) << std::endl;
  std::cout << "d" << d1 << " " << get_phase_single(d2, 4, 2) << std::endl;
  std::cout << "d" << d1 << " " << get_phase_single(d3, 2, 4) << std::endl;
  std::cout << "d" << d1 << " " << get_phase_single(d4, 2, 4) << std::endl;
}
