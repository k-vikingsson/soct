/*******************************************************************************
 * Array expansion domain
 * 
 * For a given array, map sequences of consecutive bytes to cells
 * consisting of a triple <offset, size, var> where:
 * 
 * - offset is an unsigned number
 * - size  is an unsigned number
 * - var is a scalar variable that represents the content of 
 *   a[offset,...,offset+size-1]
 * 
 * The domain is general enough to represent any possible sequence of
 * consecutive bytes including sequences of bytes starting at the same
 * offsets but different sizes, overlapping sequences starting at
 * different offsets, etc. However, there are some cases that have
 * been implemented an imprecise manner:
 * 
 * (1) array store/load with a non-constant index are conservatively ignored.
 * (2) array load from a cell that overlaps with other cells return top.
 ******************************************************************************/

#pragma once

#include <crab/common/types.hpp>
#include <crab/common/debug.hpp>
#include <crab/common/stats.hpp>
#include <crab/domains/operators_api.hpp>
#include <crab/domains/domain_traits.hpp>

#include <crab/domains/intervals.hpp>
#include <crab/domains/patricia_trees.hpp>
#include <crab/domains/separate_domains.hpp>

#include <algorithm>
#include <vector>
#include <set>
#include <boost/optional.hpp>
#include "boost/range/algorithm/set_algorithm.hpp"


namespace crab {
namespace domains {
      
  // forward declarations
  template<typename Variable> class offset_map;
  template<typename Domain> class array_expansion_domain;
  
  // wrapper for using ikos::index_t as patricia_tree keys
  class offset_t {
    ikos::index_t _val;
    
  public:
    explicit offset_t(ikos::index_t v)
      : _val(v) {}
    
    ikos::index_t index() const
    { return _val; }
    
    bool operator<(const offset_t& o) const
    { return _val < o._val; }
    
    bool operator==(const offset_t& o) const
       { return _val == o._val; }
    
    bool operator!=(const offset_t& o) const
    { return !(*this == o); }
    
    void write(crab::crab_os& o) const
    { o << _val; }
    
    friend crab::crab_os& operator<<(crab::crab_os& o, const offset_t& v) {
      v.write(o);
      return o;
    }
  };
  

  /* 
     Conceptually, a cell is tuple of an array, offset, size, and
     scalar variable such that:

         _scalar = array[_offset, _offset+1,...,_offset+_size-1]
	 
     For simplicity, we don't carry the array inside the cell class.
     Only, offset_map objects can create cells. They will consider the
     array when generating the scalar variable.
  */
  template<typename Variable>
  class cell {
  private:
    friend class offset_map<Variable>;
    typedef cell<Variable> cell_t;
    typedef ikos::interval<typename Variable::number_t> interval_t;
    
    offset_t _offset;
    unsigned _size;
    boost::optional<Variable> _scalar;
    
    // Only offset_map<Variable> can create cells
    cell()
      : _offset(0)
      , _size(0)
      , _scalar(boost::optional<Variable>()) { }
    cell(offset_t offset, Variable scalar)
      : _offset(offset)
      , _size(scalar.get_bitwidth())  
      , _scalar(scalar) { }
    
    cell(offset_t offset, unsigned size)
      : _offset(offset)
      , _size(size)  
      , _scalar(boost::optional<Variable>()) { }
    
    static interval_t to_interval(const offset_t o, unsigned size)  {
      interval_t i(o.index(), o.index() + size - 1);
      return i;
    }
       
    interval_t to_interval() const {
      return to_interval(get_offset(), get_size());
    }
    
  public:
    
    bool is_null() const { return (_offset.index() == 0 && _size == 0); }
    
    offset_t get_offset() const { return _offset; }
    
    size_t get_size() const { return _size; }
    
    bool has_scalar() const { return (bool) _scalar; }
    
    Variable get_scalar() const {
      if (!has_scalar()) {
	CRAB_ERROR("cannot get undefined scalar variable");
      }
      return *_scalar;
    }
    
    // inclusion test
    bool operator<=(const cell_t& o) const {
      interval_t x = to_interval();
      interval_t y = o.to_interval();
	 return x <= y;
    }
    
    // ignore the scalar variable
    bool operator==(const cell_t& o) const {
      return (get_offset() == o.get_offset() && get_size() == o.get_size());
    }
    
    // ignore the scalar variable
    bool operator<(const cell_t& o) const {
      if (get_offset() < o.get_offset()) {
	return true;
      } else if (get_offset() == o.get_offset()) {
	return get_size() < o.get_size();
      } else {
	return false;
      }
    }
    
    bool overlap(const offset_t& o, unsigned size) const {
      interval_t x = to_interval();
      interval_t y = to_interval(o, size);
      bool res = (!(x & y).is_bottom());
      CRAB_LOG("array-expansion-overlap",
		  crab::outs() << "**Checking if " << x << " overlaps with "
		               << y << "=" << res << "\n";);
      return res;
    }
    
    void write(crab::crab_os& o) const {
      o << to_interval() << " -> ";
      if (has_scalar()) {
	o << get_scalar();
      } else { 
	o << "_";
      }
    }
    
    friend crab::crab_os& operator<<(crab::crab_os& o, const cell_t& c) {
      c.write(o);
      return o;
    }
  };

  namespace cell_set_impl {
    template <typename Set>
    inline Set set_intersection(Set& s1, Set& s2) {
      Set s3;
      boost::set_intersection(s1, s2, std::inserter(s3, s3.end()));
      return s3;
    }
    
    template <typename Set>
    inline Set set_union(Set& s1, Set& s2) {
      Set s3;
      boost::set_union(s1, s2, std::inserter(s3, s3.end()));
      return s3;
    }
    
    template <typename Set>
    inline bool set_inclusion(Set& s1, Set& s2) {
      Set s3;
      boost::set_difference(s1, s2, std::inserter(s3, s3.end()));
      return s3.empty();
    }
  }

  // Map offsets to cells
  template<typename Variable>
  class offset_map {
  public:
    
    typedef cell<Variable> cell_t;
    
  private:
    
    template<typename Dom>
    friend class array_expansion_domain;
    
    typedef offset_map<Variable> offset_map_t;
    typedef std::set<cell_t> cell_set_t;
    typedef crab::variable_type type_t;
    
    /*
      The keys in the patricia tree are processing in big-endian
      order. This means that the keys are sorted. Sortedeness is
      very important to perform efficiently operations such as
      checking for overlap cells. Since keys are treated as bit
      patterns, negative offsets can be used but they are treated
      as large unsigned numbers.
    */
    typedef patricia_tree<offset_t, cell_set_t> patricia_tree_t;
    typedef typename patricia_tree_t::binary_op_t binary_op_t;
    typedef typename patricia_tree_t::partial_order_t partial_order_t;
    
    patricia_tree_t _map;
    
    // for algorithm::lower_bound and algorithm::upper_bound
    struct compare_binding_t {
      bool operator()(const typename patricia_tree_t::binding_t& kv,
		      const offset_t& o) const {
	return kv.first < o;
      }
      bool operator()(const offset_t& o,
		      const typename patricia_tree_t::binding_t& kv) const {
	return o < kv.first;
      }
      bool operator()(const typename patricia_tree_t::binding_t& kv1,
		      const typename patricia_tree_t::binding_t& kv2) const {
	return kv1.first < kv2.first;	   
      }
    };
    
    patricia_tree_t apply_operation(binary_op_t& o,
				    patricia_tree_t t1, patricia_tree_t t2){
      t1.merge_with(t2, o);
      return t1;
    }
    
    class join_op: public binary_op_t {
      // apply is called when two bindings (one each from a
      // different map) have the same key (i.e., offset).
      boost::optional<cell_set_t> apply(cell_set_t x, cell_set_t y) {
	return cell_set_impl::set_union(x,y);
      }
      // if one map does not have a key in the other map we add it.
      bool default_is_absorbing() {
	return false;
      }
    }; 

    class meet_op: public binary_op_t {
      boost::optional<cell_set_t> apply(cell_set_t x, cell_set_t y) {
	return cell_set_impl::set_intersection(x,y);	
      }
      // if one map does not have a key in the other map we ignore
      // it.
      bool default_is_absorbing() {
	return true;
      }
    }; 
    
    class domain_po: public partial_order_t {
      bool leq(cell_set_t x, cell_set_t y) {
	return cell_set_impl::set_inclusion(x,y);		
      }
      // default value is bottom (i.e., empty map)
      bool default_is_top() {
	return false;
      }
    }; // class domain_po
    
    offset_map(patricia_tree_t m): _map(m) { }

    void remove_cell(const cell_t& c) {
      if (boost::optional<cell_set_t> cells = _map.lookup(c.get_offset())) {
	if ((*cells).erase(c) > 0) {
	  _map.remove(c.get_offset());
	  if (!(*cells).empty()) {
	    // a bit of a waste ...	    
	    _map.insert(c.get_offset(), *cells);
	  }
	}
      }
    }

    void insert_cell(const cell_t& c, bool sanity_check = true) {
      if (sanity_check && !c.has_scalar()) {
	CRAB_ERROR("array expansion cannot insert a cell without scalar variable");
      }
      if (boost::optional<cell_set_t> cells = _map.lookup(c.get_offset())) {
	if ((*cells).insert(c).second) {
	  // a bit of a waste ...
	  _map.remove(c.get_offset());
	  _map.insert(c.get_offset(), *cells);
	}
      } else {
	cell_set_t new_cells;
	new_cells.insert(c);
	_map.insert(c.get_offset(), new_cells);
      }
    }

    cell_t get_cell(offset_t o, unsigned size) const {
      if (boost::optional<cell_set_t> cells = _map.lookup(o)) {
	cell_t tmp(o, size);
	auto it = (*cells).find(tmp);
	if (it != (*cells).end()){
	  return *it;
	}
      }
      // not found
      return cell_t();
    }

    static std::string mk_scalar_name(Variable a, offset_t o, unsigned size) {
      crab::crab_string_os os;
      os << a << "[";
      if (size == 1) {
	os << o;
      } else {
	os << o << "..." << o.index() + size -1;
      }
      os << "]";
      return os.str();
    }

    static type_t get_array_element_type(type_t array_type) {
      if (array_type == ARR_BOOL_TYPE) {
	return BOOL_TYPE;
      } else if (array_type == ARR_INT_TYPE) {
	return INT_TYPE;
      } else if (array_type == ARR_REAL_TYPE) {
	return REAL_TYPE;
      } else {
	assert(array_type == ARR_PTR_TYPE);
	return PTR_TYPE;
      }
    }    

    // global state to map the same triple of array, offset and size
    // to same index
    static std::map<std::pair<ikos::index_t, std::pair<offset_t, unsigned>>,
		    ikos::index_t> _index_map;
    
    ikos::index_t get_index(Variable a, offset_t o, unsigned size) {
      auto it = _index_map.find({a.index(), {o, size}});
      if (it != _index_map.end()) {
	return it->second;
      } else {
	ikos::index_t res = _index_map.size();
	_index_map.insert({{a.index(), {o, size}},res});
	return res;
      }
    }
    
    cell_t mk_cell(Variable array, offset_t o, unsigned size) {
      // TODO: check array is the array associated to this offset map
      
      cell_t c = get_cell(o, size);
      if (c.is_null()) {
	auto& vfac = array.name().get_var_factory();
	std::string vname = mk_scalar_name(array, o, size);
	type_t vtype = get_array_element_type(array.get_type());
	ikos::index_t vindex = get_index(array, o, size);
	
	// create a new scalar variable for representing the contents
	// of bytes array[o,o+1,..., o+size-1]
	Variable scalar_var(vfac.get(vindex, vname), vtype, size);
	c = cell_t(o, scalar_var);
	insert_cell(c);
	CRAB_LOG("array-expansion", crab::outs() << "**Created cell " << c << "\n";);
      }
      // sanity check
      if (!c.has_scalar()) {
	CRAB_ERROR("array expansion created a new cell without a scalar");
      }
      return c;
    }
    
  public:
    
    offset_map() {}
    
    bool empty() const { return _map.empty(); }

    std::size_t size() const { return _map.size(); }

    // leq operator
    bool operator<=(const offset_map_t& o) const {
      domain_po po;
      return _map.leq(o._map, po);
    }
    
    // set union: if two cells with same offset do not agree on
    // size then they are ignored.
    offset_map_t operator|(const offset_map_t& o) {
      join_op op;
      return offset_map_t(apply_operation(op, _map, o._map));
    }

    // set intersection: if two cells with same offset do not agree
    // on size then they are ignored.
    offset_map_t operator&(const offset_map_t& o) {
      meet_op op;
      return offset_map_t(apply_operation(op, _map, o._map));
    }

    void operator-=(const cell_t& c) {
      remove_cell(c);
    }

    void operator-=(const std::vector<cell_t>& cells) {
      for (unsigned i=0, e=cells.size(); i<e; ++i) {
	this->operator-=(cells[i]);
      }
    }
    
    // Return in out all cells that might overlap with (o, size).
    void get_overlap_cells(offset_t o, unsigned size, std::vector<cell_t>& out){
      compare_binding_t comp;

      bool added = false;
      cell_t c = get_cell(o, size);
      if (c.is_null()) {
	// we need to add a temporary cell for (o, size)
	c = cell_t(o, size);
	insert_cell(c, false/*disable sanity check*/);
	added = true;
      }

      auto lb_it = std::lower_bound(_map.begin(), _map.end(), o, comp);
      if (lb_it != _map.end()) {
	// Store _map[begin,...,lb_it] into a vector so that we can
	// go backwards from lb_it.
	//
	// TODO: give support for reverse iterator in patricia_tree.	   
	std::vector<cell_set_t> upto_lb;
	upto_lb.reserve(std::distance(_map.begin(), lb_it));
	for(auto it=_map.begin(), et=lb_it; it!=et; ++it) {
	  upto_lb.push_back(it->second);
	}
	upto_lb.push_back(lb_it->second);

	for (int i=upto_lb.size()-1; i >= 0; --i) {
	  ///////
	  // All the cells in upto_lb[i] have the same offset. They
	  // just differ in the size.
	  // 
	  // If none of the cells in upto_lb[i] overlap with (o, size)
	  // we can stop.
	  ////////
	  bool continue_outer_loop = false;
	  for (const cell_t& x: upto_lb[i]) {
	    if (x.overlap(o, size)) {
	      if (!(x == c)) {
		// FIXME: we might have some duplicates. this is a very drastic solution.
		if (std::find(out.begin(), out.end(), x) == out.end()) {	      
		  out.push_back(x);
		}
	      }
	      continue_outer_loop = true;
	    }
	  }
	  if (!continue_outer_loop) {
	    break;
	  }
	}
      }
      
      // search for overlapping cells > o
      auto ub_it = std::upper_bound(_map.begin(), _map.end(), o, comp);
      for(; ub_it != _map.end(); ++ub_it) {
	bool continue_outer_loop = false;
	for (const cell_t& x: ub_it->second) {
	  if (x.overlap(o, size)) {
	    // FIXME: we might have some duplicates. this is a very drastic solution.	    
	    if (std::find(out.begin(), out.end(), x) == out.end()) {
	      out.push_back(x);
	    }
	    continue_outer_loop = true;
	  }
	}
	if (!continue_outer_loop) {
	  break;
	}
      }

      // do not forget the rest of overlapping cells == o
      for (auto it = ++lb_it, et = ub_it; it!=et; ++it) {
	bool continue_outer_loop = false;
	for (const cell_t& x: it->second) {
	  if (x == c) { // we dont put it in out
	    continue;
	  }
	  if (x.overlap(o, size)) {
	    if (!(x == c)) {
	      if (std::find(out.begin(), out.end(), x) == out.end()) {
		out.push_back(x);
	      }
	    }
	    continue_outer_loop = true;
	  }
	}
	if (!continue_outer_loop) {
	  break;
	}
      }
	 
      if (added) {
	// remove the temporary cell for (o, size)
	assert(!c.is_null());
	remove_cell(c);
      }
	 
      CRAB_LOG("array-expansion-overlap",
	       crab::outs() << "**Overlap set between \n" << *this << "\nand "
	       << "(" << o << "," << size <<")={";
	       for(unsigned i=0, e=out.size(); i<e;) {
		 crab::outs () << out[i];
		 ++i;
		 if (i<e) {
		   crab::outs () << ",";
		 }
	       }
	       crab::outs() << "}\n";);
    }
       
    void write(crab::crab_os& o) const {
      if (_map.empty()) {
	o << "empty";
      } else {
	for(auto it=_map.begin(), et=_map.end(); it!=et; ++it) {
	  const cell_set_t& cells = it->second;
	  o << "{";
	  for (auto cit=cells.begin(), cet=cells.end(); cit!=cet; ) {
	    o << *cit;
	    ++cit;
	    if (cit != cet) {
	      o << ",";
	    }
	  }
	  o << "}\n";
	}
      }
    }

    friend crab::crab_os& operator<<(crab::crab_os& o, const offset_map_t& m) {
      m.write(o);
      return o;
    }

    /* Operations needed if used as value in a separate_domain */
    bool operator==(const offset_map_t& o) const
    { return *this <= o && o <= *this; }
    bool is_top() const { return empty(); } 
    bool is_bottom() const { return false; }
    /* 
       We don't distinguish between bottom and top.
       This is fine because separate_domain only calls bottom if
       operator[] is called over a bottom state. Thus, we will make
       sure that we don't call operator[] in that case.
    */
    static offset_map_t bottom() { return offset_map_t();}
    static offset_map_t top() { return offset_map_t();}       
  };

  template<typename Var>
  std::map<std::pair<ikos::index_t, std::pair<offset_t, unsigned>>,
	   ikos::index_t> offset_map<Var>::_index_map;
       
  template<typename NumDomain>
  class array_expansion_domain:
    public abstract_domain<typename NumDomain::number_t,
			   typename NumDomain::varname_t,
			   array_expansion_domain<NumDomain> > {
       
  public:
       
    typedef typename NumDomain::number_t Number;
    typedef typename NumDomain::varname_t VariableName;
       
  private:
       
    typedef array_expansion_domain<NumDomain> array_expansion_domain_t;
    typedef abstract_domain<Number,VariableName,array_expansion_domain_t> abstract_domain_t;
       
  public:
       
    using typename abstract_domain_t::linear_expression_t;
    using typename abstract_domain_t::linear_constraint_t;
    using typename abstract_domain_t::linear_constraint_system_t;
    using typename abstract_domain_t::disjunctive_linear_constraint_system_t;	
    using typename abstract_domain_t::variable_t;
    using typename abstract_domain_t::number_t;
    using typename abstract_domain_t::varname_t;
    using typename abstract_domain_t::variable_vector_t;	
    typedef crab::pointer_constraint<variable_t> ptr_cst_t;
    typedef NumDomain content_domain_t;
    typedef interval <Number> interval_t;
       
  private:
    typedef bound <Number> bound_t;
    typedef crab::variable_type type_t;
    typedef offset_map<variable_t> offset_map_t;
    typedef cell<variable_t> cell_t;
    typedef separate_domain<variable_t, offset_map_t> separate_domain_t;

    // map from array variable to map from offsets to scalar variables
    separate_domain_t _array_map;
    // scalar domain
    NumDomain _inv; 
    
    array_expansion_domain (separate_domain_t array_map, NumDomain inv)
      : _array_map(array_map), _inv (inv) { }
	 
    interval_t to_interval(linear_expression_t expr) {
      interval_t r(expr.constant());
      for (typename linear_expression_t::iterator it = expr.begin(); 
           it != expr.end(); ++it) {
	interval_t c(it->first);
	r += c * _inv[it->second];
      }
      return r;
    }
    
  public:
       
    array_expansion_domain()
      : _array_map(separate_domain_t::top()), _inv (NumDomain::top()) { }  
       
    static array_expansion_domain_t top() { 
      return array_expansion_domain (separate_domain_t::top(), NumDomain::top ()); 
    }
       
    static array_expansion_domain_t bottom() {
      return array_expansion_domain (separate_domain_t::bottom(), NumDomain::bottom ());
    }
       
    array_expansion_domain (const array_expansion_domain_t& other): 
      _array_map(other._array_map), _inv (other._inv) { 
      crab::CrabStats::count (getDomainName() + ".count.copy");
      crab::ScopedCrabStats __st__(getDomainName() + ".copy");
    }
        
    array_expansion_domain_t& operator=(const array_expansion_domain_t& other) {
      crab::CrabStats::count (getDomainName() + ".count.copy");
      crab::ScopedCrabStats __st__(getDomainName() + ".copy");
      if (this != &other) {
	_array_map = other._array_map;
	_inv = other._inv;
      }
      return *this;
    }
       
    bool is_bottom() { 
      return (_inv.is_bottom ());
    }
       
    bool is_top() { 
      return (_inv.is_top());
    }
       
    bool operator<=(array_expansion_domain_t other) {
      return (_inv <= other._inv);
    }
       
    void operator|=(array_expansion_domain_t other) {
      _array_map = _array_map | other._array_map;
      _inv |= other._inv;
	 
    }
       
    array_expansion_domain_t operator|(array_expansion_domain_t other) {
      return array_expansion_domain_t (_array_map | other._array_map, _inv | other._inv);
    }
       
    array_expansion_domain_t operator&(array_expansion_domain_t other) {
      return array_expansion_domain_t (_array_map & other._array_map, _inv & other._inv);
    }
       
    array_expansion_domain_t operator||(array_expansion_domain_t other) {
      return array_expansion_domain_t (_array_map | other._array_map, _inv || other._inv);
    }
       
    template<typename Thresholds>
    array_expansion_domain_t widening_thresholds (array_expansion_domain_t other, 
						  const Thresholds &ts) {
      return array_expansion_domain_t (_array_map | other._array_map,
				       _inv.widening_thresholds (other._inv, ts));
    }
    
    array_expansion_domain_t operator&& (array_expansion_domain_t other) {
      return array_expansion_domain_t (_array_map & other._array_map, _inv && other._inv);
    }
        
       
    // remove all variables [begin,...end)
    template<typename Iterator>
    void forget (Iterator begin, Iterator end) {
      domain_traits<NumDomain>::forget (_inv, begin, end);
      
      for (auto it = begin, et = end; it!=et; ++it) {
	if ((*it).is_array_type()) {
	  _array_map -= *it;
	}
      }
    }
       
    // dual of forget: remove all variables except [begin,...end)
    template<typename Iterator>
    void project (Iterator begin, Iterator end) {
      domain_traits<NumDomain>::project (_inv, begin, end);
      
      for (auto it = begin, et = end; it!=et; ++it) {
	if ((*it).is_array_type()) {
	  CRAB_WARN("TODO: project onto an array variable");
	}
      }
    }
       
    void operator += (linear_constraint_system_t csts) {
      _inv += csts;
      
      CRAB_LOG("array-expansion",
	       crab::outs() << "assume(" << csts << ")  " << *this <<"\n";);
    }
       
    void operator-=(variable_t var) {
      if (var.is_array_type()) {
	_array_map -= var;
      } else {
	_inv -= var;
      }
    }
       
    void assign (variable_t x, linear_expression_t e) {
      _inv.assign (x, e);
         
      CRAB_LOG("array-expansion",
	       crab::outs() << "apply "<< x<< " := "<< e<< " " << *this <<"\n";);
    }
       
    void apply (operation_t op, variable_t x, variable_t y, Number z) {
      _inv.apply (op, x, y, z);
         
      CRAB_LOG("array-expansion",
	       crab::outs() << "apply "<< x<< " := "<< y<< " "<< op
	       << " "<< z << " " << *this <<"\n";);
    }
       
    void apply(operation_t op, variable_t x, variable_t y, variable_t z) {
      _inv.apply (op, x, y, z);
          
      CRAB_LOG("array-expansion",
	       crab::outs() << "apply "<< x<< " := "<< y<< " "<< op
	       << " " << z << " " << *this << "\n";);
    }
       
    void apply(operation_t op, variable_t x, Number k) {
      _inv.apply (op, x, k);
	  
      CRAB_LOG("array-expansion",
	       crab::outs() << "apply "<< x<< " := "<< x<< " "<< op
	       << " " << k << " " << *this  <<"\n";);
    }
       
    void backward_assign (variable_t x, linear_expression_t e,
			  array_expansion_domain_t inv) {
      _inv.backward_assign(x, e, inv.get_content_domain());
    }
	
    void backward_apply (operation_t op,
			 variable_t x, variable_t y, Number z,
			 array_expansion_domain_t inv) {
      _inv.backward_apply(op, x, y, z, inv.get_content_domain());
    }
       
    void backward_apply(operation_t op,
			variable_t x, variable_t y, variable_t z,
			array_expansion_domain_t inv) {
      _inv.backward_apply(op, x, y, z, inv.get_content_domain());
    }
       
    void apply(int_conv_operation_t op, variable_t dst, variable_t src) {
      _inv.apply (op, dst, src);
    }
       
    void apply(bitwise_operation_t op, variable_t x, variable_t y, variable_t z) {
      _inv.apply (op, x, y, z);
	 
      CRAB_LOG("array-expansion",
	       crab::outs() << "apply "<< x<< " := "<< y<< " " << op 
	       << " " << z << " " << *this << "\n";);
    }
       
    void apply(bitwise_operation_t op, variable_t x, variable_t y, Number k) {
      _inv.apply (op, x, y, k);
	 
      CRAB_LOG("array-expansion",
	       crab::outs() << "apply "<< x<< " := "<< y<< " " << op
	       << " "<< k << " " << *this <<"\n";);
    }
       
    // division_operators_api
    void apply(div_operation_t op, variable_t x, variable_t y, variable_t z) {
      _inv.apply (op, x, y, z);
	 
      CRAB_LOG("array-expansion",
	       crab::outs() << "apply "<< x<< " := "<< y<< " "<< op
	       << " " << z << " " << *this <<"\n";);
    }
       
    void apply(div_operation_t op, variable_t x, variable_t y, Number k) {
      _inv.apply (op, x, y, k);
	 
      CRAB_LOG("array-expansion",
	       crab::outs() << "apply "<< x<< " := "<< y<< " "<< op
	       << " " << k << " " << *this <<"\n";);
    }
       
    // boolean operators
    virtual void assign_bool_cst (variable_t lhs, linear_constraint_t rhs) override {
      _inv.assign_bool_cst (lhs, rhs);
    }    
       
    virtual void assign_bool_var(variable_t lhs, variable_t rhs, bool is_not_rhs) override {
      _inv.assign_bool_var (lhs, rhs, is_not_rhs);
    }    
       
    virtual void apply_binary_bool (bool_operation_t op,variable_t x,
				    variable_t y,variable_t z) override {
      _inv.apply_binary_bool (op, x, y, z);
    }    
	
    virtual void assume_bool (variable_t v, bool is_negated) override {
      _inv.assume_bool (v, is_negated);
    }    
       
    // backward boolean operators
    virtual void backward_assign_bool_cst(variable_t lhs, linear_constraint_t rhs,
					  array_expansion_domain_t inv){
      _inv.backward_assign_bool_cst(lhs, rhs, inv.get_content_domain());	  
    }
       
    virtual void backward_assign_bool_var(variable_t lhs, variable_t rhs, bool is_not_rhs,
					  array_expansion_domain_t inv) {
      _inv.backward_assign_bool_var(lhs, rhs, is_not_rhs, inv.get_content_domain());
    }
       
    virtual void backward_apply_binary_bool(bool_operation_t op,
					    variable_t x,variable_t y,variable_t z,
					    array_expansion_domain_t inv) {
      _inv.backward_apply_binary_bool(op, x, y, z, inv.get_content_domain());
    }
       
    // pointer_operators_api
    virtual void pointer_load (variable_t lhs, variable_t rhs) override {
      _inv.pointer_load(lhs,rhs);
    }
       
    virtual void pointer_store (variable_t lhs, variable_t rhs) override {
      _inv.pointer_store(lhs,rhs);
    } 
       
    virtual void pointer_assign (variable_t lhs, variable_t rhs,
				 linear_expression_t offset) override {
      _inv.pointer_assign (lhs,rhs,offset);
    }
        
    virtual void pointer_mk_obj (variable_t lhs, ikos::index_t address) override {
      _inv.pointer_mk_obj (lhs, address);
    }
       
    virtual void pointer_function (variable_t lhs, VariableName func) override {
      _inv.pointer_function (lhs, func);
    }
       
    virtual void pointer_mk_null (variable_t lhs) override {
      _inv.pointer_mk_null (lhs);
    }
       
    virtual void pointer_assume (ptr_cst_t cst) override {
      _inv.pointer_assume (cst);
    }    
       
    virtual void pointer_assert (ptr_cst_t cst) override {
      _inv.pointer_assert (cst);
    }    
       
    // array_operators_api 
       
    // All the array elements are assumed to be equal to val
    virtual void array_init (variable_t a,
			     linear_expression_t elem_size,
			     linear_expression_t lb_idx,
			     linear_expression_t ub_idx, 
			     linear_expression_t val) override {

      crab::CrabStats::count (getDomainName() + ".count.array_init");
      crab::ScopedCrabStats __st__(getDomainName() + ".array_init");

      if (is_bottom() || is_top()) return;
      
      interval_t lb_i = to_interval(lb_idx);
      auto lb = lb_i.singleton();
      if (!lb) {
	CRAB_WARN("array expansion initialization ignored because ",
		  "lower bound is not constant");
	return;
      }
      
      interval_t ub_i = to_interval(ub_idx);
      auto ub = ub_i.singleton();
      if (!ub) {
	CRAB_WARN("array expansion initialization ignored because ",
		  "upper bound is not constant");
	return;
      }

      interval_t n_i = to_interval(elem_size);
      auto n = n_i.singleton();
      if (!n) {
	CRAB_WARN("array expansion initialization ignored because ",
		  "elem size is not constant");
		  
	return;
      }
	
      if ((*ub - *lb) % *n != 0) {
	CRAB_WARN("array expansion initialization ignored because ",
		  "the number of elements must be divisible by ", *n);
	return;
      }

      const number_t max_num_elems = 512;
      if (*ub - *lb > max_num_elems) {
	CRAB_WARN("array expansion initialization ignored because ",
		  "the number of elements is larger than default limit of ",
		  max_num_elems);
	return;
      }

      for(number_t i = *lb, e = *ub; i < e; ) {
	array_store(a, elem_size, i, val, false);
	i = i + *n;
      }
      
      CRAB_LOG("array-expansion",
	       crab::outs() << a << "[" << lb_idx << "..." << ub_idx << "] := " << val
	                    << " -- " << *this <<"\n";);
    }
    
    virtual void array_load (variable_t lhs, variable_t a,
			     linear_expression_t elem_size,
			     linear_expression_t i) override {
      crab::CrabStats::count (getDomainName() + ".count.load");
      crab::ScopedCrabStats __st__(getDomainName() + ".load");

      if (is_bottom() || is_top()) return;

      interval_t ii = to_interval(i);
      if (boost::optional<number_t> n = ii.singleton()) {
	offset_map_t offset_map = _array_map[a];
	offset_t o((long) *n);	
	interval_t i_elem_size = to_interval(elem_size);
	boost::optional<number_t> n_bytes = i_elem_size.singleton();
	if (!n_bytes) {
	  CRAB_WARN("array expansion ignored array load because element size is not constant");
	  return;
	}
	unsigned size = (long)*n_bytes;
	
	std::vector<cell_t> cells;
	offset_map.get_overlap_cells(o, size, cells);
	if (!cells.empty()) {
	  CRAB_WARN("array expansion ignored read from cell [",
		    o, "...",o.index()+size-1,"]",
		    " because it overlaps with other cells.");
	  /*
	    TODO: we can apply here "Value Recomposition" 'a la'
	    Mine'06 to construct values of some type from a sequence
	    of bytes. It can be endian-independent but it would more
	    precise if we choose between little- and big-endian.
	  */
	} else {
	  cell_t c = offset_map.mk_cell(a, o, size);
	  assert(c.has_scalar());
	  // Here it's ok to do assignment because c is not a summarized
	  // variable. Otherwise, it would be unsound.
	  _inv.assign(lhs, c.get_scalar());
	  _array_map.set(a, offset_map);
	  
	  goto array_load_end;
	}
      } else {
	// TODO
	CRAB_WARN("array expansion: ignored read because of non-constant array index ",i);
      }

      _inv -= lhs;

    array_load_end:
      CRAB_LOG("array-expansion",
	       crab::outs() << lhs << ":=" << a <<"[" << i << "]  -- "
	       << *this <<"\n";);
    }
        
        
    virtual void array_store (variable_t a, linear_expression_t elem_size,
			      linear_expression_t i, linear_expression_t val, 
			      bool /*is_singleton*/) override {
      crab::CrabStats::count (getDomainName() + ".count.store");
      crab::ScopedCrabStats __st__(getDomainName() + ".store");

      if (is_bottom()) return;

      interval_t ii = to_interval(i);
      if (boost::optional<number_t> n = ii.singleton()) {
	offset_map_t offset_map = _array_map[a];
	offset_t o((long) *n);

	interval_t i_elem_size = to_interval(elem_size);
	boost::optional<number_t> n_bytes = i_elem_size.singleton();
	if (!n_bytes) {
	  CRAB_WARN("array expansion ignored array store because element size is not constant");
	  return;
	}
	unsigned size = (long)*n_bytes;
	
	// kill overlapping cells
	std::vector<cell_t> cells;
	offset_map.get_overlap_cells(o, size, cells);
	if (cells.size() > 0) {
	  CRAB_LOG("array-expansion",
		   CRAB_WARN("array expansion killed ", cells.size(),
			     " overlapping cells with ",
			     "[", o, "...", o.index()+size-1,"]", " before writing."));
	  
	  // Forget the scalars from the numerical domain
	  for (unsigned i=0, e=cells.size(); i<e; ++i) {
	    const cell_t& c = cells[i];
	    if (c.has_scalar()) {
	      _inv -= c.get_scalar();
	    } else {
	      CRAB_ERROR("array expansion: cell without scalar variable in array store");
	    }
	  }
	  // Remove the cell. If needed again it will be re-created.
	  offset_map -= cells;
	}
	
	// Perform scalar update	   
	// -- create a new cell it there is no one already
	cell_t c = offset_map.mk_cell(a, o, size);
	// -- strong update
	_inv.assign(c.get_scalar(), val);
	_array_map.set(a, offset_map);
	
      } else {
	// TODO: weak update
	CRAB_WARN("array expansion: ignored write because of non-constant array index ", i);
      }

      CRAB_LOG("array-expansion",
	       crab::outs() << a << "[" << i << "]:="
	       << val << " -- " << *this <<"\n";);
    }
       
    virtual void array_assign (variable_t lhs, variable_t rhs) override {
      _array_map[lhs] = _array_map[rhs];
    }
       
    linear_constraint_system_t to_linear_constraint_system (){
      return _inv.to_linear_constraint_system ();
    }
       
    disjunctive_linear_constraint_system_t
    to_disjunctive_linear_constraint_system (){
      return _inv.to_disjunctive_linear_constraint_system ();
    }
       
    NumDomain get_content_domain () const {      
      return _inv;
    }
       
    void write(crab_os& o) {
      o << _inv;
    }
       
    static std::string getDomainName () {
      std::string name ("ArrayExpansion(" + 
			NumDomain::getDomainName () + 
			")");
      return name;
    }  
       
    void rename(const variable_vector_t& from, const variable_vector_t &to){
      _inv.rename(from, to);
      for (auto &v: from) {
	if (v.is_array_type()) {
	  CRAB_WARN("TODO: rename array variable");
	}
      }
    }
       
  }; // end array_expansion_domain
  
  template<typename BaseDomain>
  class domain_traits<array_expansion_domain<BaseDomain>> {
  public:
       
    typedef array_expansion_domain<BaseDomain> array_expansion_domain_t;
    typedef typename BaseDomain::varname_t VariableName;
    typedef typename BaseDomain::variable_t variable_t;
       
       
    template<class CFG>
    static void do_initialization (CFG cfg) { }
       
    static void normalize (array_expansion_domain_t& inv) { 
      CRAB_WARN ("array expansion normalize not implemented");
    }
       
    template <typename Iter>
    static void forget (array_expansion_domain_t& inv, Iter it, Iter end) {
      inv.forget (it, end);
    }
       
    template <typename Iter >
    static void project (array_expansion_domain_t& inv, Iter it, Iter end) {
      inv.project (it, end);
    }

    static void expand (array_expansion_domain_t& inv, variable_t x, variable_t new_x) {
      // -- lose precision if relational or disjunctive domain
      CRAB_WARN ("array expansion expand not implemented");
    }

  };

  template<typename BaseDom>
  class checker_domain_traits<array_expansion_domain<BaseDom>> {
  public:
    typedef array_expansion_domain<BaseDom> this_type;
    typedef typename this_type::linear_constraint_t linear_constraint_t;
      
    static bool entail(this_type& inv, const linear_constraint_t& cst) {
      BaseDom dom = inv.get_content_domain();
      return checker_domain_traits<BaseDom>::entail(dom, cst);
    }
    static bool intersect(this_type& inv, const linear_constraint_t& cst) {
      BaseDom dom = inv.get_content_domain();	
      return checker_domain_traits<BaseDom>::intersect(dom, cst);
    }
      
  };
     
} // namespace domains
}// namespace crab
