#include "../program_options.hpp"
#include "../common.hpp"

using namespace std;
using namespace crab::analyzer;
using namespace crab::cfg_impl;
using namespace crab::domain_impl;

/* Example of how to build a CFG */
z_cfg_t* prog1 (variable_factory_t &vfac)  {

  // Definining program variables
  z_var i (vfac ["i"], crab::INT_TYPE, 32);
  z_var k (vfac ["k"], crab::INT_TYPE, 32);
  z_var x1 (vfac ["x1"], crab::INT_TYPE, 32);
  z_var x2 (vfac ["x2"], crab::INT_TYPE, 32);
  // entry and exit block
  z_cfg_t* cfg = new z_cfg_t("entry","ret");
  // adding blocks
  z_basic_block_t& entry = cfg->insert ("entry");
  z_basic_block_t& bb1   = cfg->insert ("bb1");
  z_basic_block_t& bb1_t = cfg->insert ("bb1_t");
  z_basic_block_t& bb1_f = cfg->insert ("bb1_f");
  z_basic_block_t& bb2   = cfg->insert ("bb2");
  z_basic_block_t& ret   = cfg->insert ("ret");
  // adding control flow
  entry >> bb1;
  bb1 >> bb1_t; bb1 >> bb1_f;
  bb1_t >> bb2; bb2 >> bb1; bb1_f >> ret;
  // adding statements
  //  entry.assign (x1, 1);
  entry.assign (k, 0);
  entry.assign (i, 0);
  bb1_t.assume (i <= 99);
  bb1_f.assume (i >= 100);
  bb2.add(i, i, 1);
  //bb2.add (x2, x1, 1);
  bb2.add(k, k, 1);
  return cfg;
}

z_cfg_t* prog2 (variable_factory_t &vfac) 
{

  z_cfg_t* cfg = new z_cfg_t("loop1_entry","ret");
  z_basic_block_t& loop1_entry = cfg->insert ("loop1_entry");
  z_basic_block_t& loop1_bb1   = cfg->insert ("loop1_bb1");
  z_basic_block_t& loop1_bb1_t = cfg->insert ("loop1_bb1_t");
  z_basic_block_t& loop1_bb1_f = cfg->insert ("loop1_bb1_f");
  z_basic_block_t& loop1_bb2   = cfg->insert ("loop1_bb2");
  z_basic_block_t& loop2_entry = cfg->insert ("loop2_entry");
  z_basic_block_t& loop2_bb1   = cfg->insert ("loop2_bb1");
  z_basic_block_t& loop2_bb1_t = cfg->insert ("loop2_bb1_t");
  z_basic_block_t& loop2_bb1_f = cfg->insert ("loop2_bb1_f");
  z_basic_block_t& loop2_bb2   = cfg->insert ("loop2_bb2");
  z_basic_block_t& ret         = cfg->insert ("ret");

  loop1_entry >> loop1_bb1;
  loop1_bb1 >> loop1_bb1_t; loop1_bb1 >> loop1_bb1_f;
  loop1_bb1_t >> loop1_bb2; loop1_bb2 >> loop1_bb1; loop1_bb1_f >> loop2_entry;

  loop2_entry >> loop2_bb1;
  loop2_bb1 >> loop2_bb1_t; loop2_bb1 >> loop2_bb1_f;
  loop2_bb1_t >> loop2_bb2; loop2_bb2 >> loop2_bb1; loop2_bb1_f >> ret;

  z_var i(vfac["i"], crab::INT_TYPE, 32);
  z_var j(vfac["j"], crab::INT_TYPE, 32);
  z_var k(vfac["k"], crab::INT_TYPE, 32);

  loop1_entry.assign (i, 0);
  loop1_entry.assign (k, 30);
  loop1_bb1_t.assume (i <= 9);
  loop1_bb1_f.assume (i >= 10);
  loop1_bb2.add (i, i, 1);

  loop2_entry.assign (j, 0);
  loop2_bb1_t.assume (j <= 9);
  loop2_bb1_f.assume (j >= 10);
  loop2_bb2.add (j, j, 1);
  return cfg;
}

z_cfg_t* prog3 (variable_factory_t &vfac) 
{

  z_cfg_t* cfg = new z_cfg_t("entry","ret");
  z_basic_block_t& entry       = cfg->insert ("entry");
  z_basic_block_t& loop1_head  = cfg->insert ("loop1_head");
  z_basic_block_t& loop1_t     = cfg->insert ("loop1_t");
  z_basic_block_t& loop1_f     = cfg->insert ("loop1_f");
  z_basic_block_t& loop1_body  = cfg->insert ("loop1_body");

  z_basic_block_t& loop1_body_t  = cfg->insert ("loop1_body_t");
  z_basic_block_t& loop1_body_f  = cfg->insert ("loop1_body_f");
  z_basic_block_t& loop1_body_x  = cfg->insert ("loop1_body_x");

  z_basic_block_t& cont        = cfg->insert ("cont");
  z_basic_block_t& loop2_head  = cfg->insert ("loop2_head");
  z_basic_block_t& loop2_t     = cfg->insert ("loop2_t");
  z_basic_block_t& loop2_f     = cfg->insert ("loop2_f");
  z_basic_block_t& loop2_body  = cfg->insert ("loop2_body");
  z_basic_block_t& ret         = cfg->insert ("ret");

  entry >> loop1_head;
  loop1_head >> loop1_t; 
  loop1_head >> loop1_f; 
  loop1_t >>    loop1_body; 

  loop1_body >> loop1_body_t;
  loop1_body >> loop1_body_f;
  loop1_body_t >> loop1_body_x;
  loop1_body_f >> loop1_body_x;
  loop1_body_x >> loop1_head;

  loop1_f >> cont;
  cont >> loop2_head;
  loop2_head >> loop2_t; 
  loop2_head >> loop2_f; 
  loop2_t >>    loop2_body; 
  loop2_body >> loop2_head;
  loop2_f >> ret;
  
  z_var i(vfac["i"], crab::INT_TYPE, 32);

  entry.assign (i, 0);
  loop1_t.assume (i <= 10);
  loop1_f.assume (i >= 11);
  loop1_body.add (i, i, 1);

  loop1_body_t.assume (i >= 9);
  loop1_body_t.assign (i , 0);
  loop1_body_f.assume (i <= 8);

  loop2_t.assume (i <= 100);
  loop2_f.assume (i >= 101);
  loop2_body.sub (i, i, 1);
  return cfg;
}

z_cfg_t* prog4 (variable_factory_t &vfac) 
{

  z_cfg_t* cfg = new z_cfg_t("entry","ret");
  z_basic_block_t& entry      = cfg->insert ("entry");
  z_basic_block_t& loop_head  = cfg->insert ("loop_head");
  z_basic_block_t& loop_t     = cfg->insert ("loop_t");
  z_basic_block_t& loop_f     = cfg->insert ("loop_f");
  z_basic_block_t& loop_body  = cfg->insert ("loop_body");
  z_basic_block_t& ret        = cfg->insert ("ret");

  entry >> loop_head;
  loop_head >> loop_t; 
  loop_head >> loop_f; 
  loop_t >> loop_body; 
  loop_body >> loop_head;
  loop_f >> ret;

  z_var i(vfac["i"], crab::INT_TYPE, 32);
  z_var p(vfac["p"], crab::INT_TYPE, 32);

  entry.assign (i, 0);
  entry.assign (p, 0);

  loop_t.assume (i <= 9);
  loop_f.assume (i >= 10);
  loop_body.add (i, i, 1);
  loop_body.add (p, p, 4);

  return cfg;
}

/* Example of how to build a CFG */
z_cfg_t* prog5 (variable_factory_t &vfac)  {

  // Definining program variables
  z_var i (vfac ["i"], crab::INT_TYPE, 32);
  z_var k (vfac ["k"], crab::INT_TYPE, 32);
  z_var nd (vfac ["nd"], crab::INT_TYPE, 32);
  // entry and exit block
  z_cfg_t* cfg = new z_cfg_t("entry","ret");
  // adding blocks
  z_basic_block_t& entry = cfg->insert ("entry");
  z_basic_block_t& bb1   = cfg->insert ("bb1");
  z_basic_block_t& bb1_t = cfg->insert ("bb1_t");
  z_basic_block_t& bb1_f = cfg->insert ("bb1_f");
  z_basic_block_t& bb2   = cfg->insert ("bb2");
  z_basic_block_t& ret   = cfg->insert ("ret");
  // adding control flow
  entry >> bb1;
  bb1 >> bb1_t; bb1 >> bb1_f;
  bb1_t >> bb2; bb2 >> bb1; bb1_f >> ret;
  // adding statements
  entry.assign (k, 0);
  entry.assign (i, 0);
  bb1_t.assume (i != 9);
  bb1_f.assume (i == 9);
  bb2.add(i, i, 1);
  bb2.add(k, k, 1);
  return cfg;
}

z_cfg_t* prog6 (variable_factory_t &vfac)  {

  // Definining program variables
  z_var k(vfac ["k"], crab::INT_TYPE, 32);
  z_var n(vfac ["n"], crab::INT_TYPE, 32);
  z_var x(vfac ["x"], crab::INT_TYPE, 32);
  z_var y(vfac ["y"], crab::INT_TYPE, 32);
  z_var t(vfac ["t"], crab::INT_TYPE, 32);
  // entry and exit block
  z_cfg_t* cfg = new z_cfg_t("entry","ret");
  // adding blocks
  z_basic_block_t& entry = cfg->insert ("entry");
  z_basic_block_t& loop = cfg->insert ("loop");
  z_basic_block_t& loop_body_1 = cfg->insert ("loop_body_1");
  z_basic_block_t& loop_body_2 = cfg->insert ("loop_body_2");
  z_basic_block_t& loop_body_3 = cfg->insert ("loop_body_3");
  z_basic_block_t& loop_body_4 = cfg->insert ("loop_body_4");  
  z_basic_block_t& ret = cfg->insert ("ret");
  // adding control flow
  entry >> loop;
  loop >> loop_body_1;
  loop_body_1 >> loop_body_2;
  loop_body_2 >> loop_body_3;
  loop_body_3 >> loop_body_4;    
  loop_body_4 >> loop;
  loop >> ret;
  // adding statements
  //  entry.assign (x1, 1);
  entry.assign(k, 200);
  entry.assign(n, 100);
  entry.assign(x, 0);
  entry.assign(y, k);
  loop_body_1.assume(x <= n - 1);
  loop_body_2.add(x, x, 1);
  loop_body_3.assign(t, 2*x);
  loop_body_4.sub(y, k , t);
  
  ret.assume(x >= n);
  ret.assertion(x + y <= k);
  return cfg;
}

/* Example of how to infer invariants from the above CFG */
int main (int argc, char** argv ) {
  SET_TEST_OPTIONS(argc,argv)
  {
    variable_factory_t vfac;
    z_cfg_t* cfg = prog1 (vfac);
    crab::outs() << *cfg << "\n";
    
    #ifdef HAVE_APRON
    run<z_oct_apron_domain_t>(cfg, cfg->entry(), false, 1, 2, 20, stats_enabled);
    #endif
    run<z_soct_domain_t>(cfg, cfg->entry(), false, 1, 2, 20, stats_enabled);
    delete cfg;
  }

  {
    variable_factory_t vfac;
    z_cfg_t* cfg = prog2 (vfac);
    crab::outs() << *cfg << "\n";
    #ifdef HAVE_APRON    
    run<z_oct_apron_domain_t>(cfg, cfg->entry(), false, 1, 2, 20, stats_enabled);
    #endif
    run<z_soct_domain_t>(cfg, cfg->entry(), false, 1, 2, 20, stats_enabled);    
    delete cfg;
  }

  {
    variable_factory_t vfac;
    z_cfg_t* cfg = prog3 (vfac);
    crab::outs() << *cfg << "\n";
    #ifdef HAVE_APRON
    run<z_oct_apron_domain_t>(cfg, cfg->entry(), false, 1, 2, 20, stats_enabled);
    #endif
    run<z_soct_domain_t>(cfg, cfg->entry(), false, 1, 2, 20, stats_enabled);    
    delete cfg;
  }

  {
    variable_factory_t vfac;
    z_cfg_t* cfg = prog4 (vfac);
    crab::outs() << *cfg << "\n";
    #ifdef HAVE_APRON
    run<z_oct_apron_domain_t>(cfg, cfg->entry(), false, 1, 2, 20, stats_enabled);
    #endif
    run<z_soct_domain_t>(cfg, cfg->entry(), false, 1, 2, 20, stats_enabled);    
    delete cfg;
  }

  {
    variable_factory_t vfac;
    z_cfg_t* cfg = prog5 (vfac);
    crab::outs() << *cfg << "\n";
    #ifdef HAVE_APRON
    run<z_oct_apron_domain_t>(cfg, cfg->entry(), false, 1, 2, 20, stats_enabled);
    #endif
    run<z_soct_domain_t>(cfg, cfg->entry(), false, 1, 2, 20, stats_enabled);    
    delete cfg;
  }

  {
    variable_factory_t vfac;
    z_cfg_t* cfg = prog6 (vfac);
    crab::outs() << *cfg << "\n";
    #ifdef HAVE_APRON
    run<z_oct_apron_domain_t>(cfg, cfg->entry(), false, 1, 2, 20, stats_enabled);
    #endif
    run<z_soct_domain_t>(cfg, cfg->entry(), false, 1, 2, 20, stats_enabled);    
    delete cfg;
  }
  
  return 0;
}
