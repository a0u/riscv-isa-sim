// See LICENSE for license details.

#include "sim.h"
#include "htif.h"
#include "cachesim.h"
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <vector>
#include <string>
#include <memory>

static void help()
{
  fputs(
    "usage: riscv-isa-run [host options] <target program> [target options]\n"
    "Host Options:\n"
    "\t-p <n>             Simulate <n> processors\n"
    "\t-m <n>             Provide <n> MB of target memory\n"
    "\t-d                 Interactive debug mode\n"
    "\t-t                 Allocate a pseudo-tty for HTIF\n"
    "\t--ic=<S>:<W>:<B>   Instantiate a cache model with S sets,\n"
    "\t--dc=<S>:<W>:<B>     W ways, and B-byte blocks (with S and\n"
    "\t--l2=<S>:<W>:<B>     B both powers of 2).\n",
    stderr);
  exit(EXIT_FAILURE);
}

int main(int argc, char** argv)
{
  
  bool debug = false;
  bool host_pty = false;
  unsigned long nprocs = 1;
  size_t mem_mb = 0;
  std::unique_ptr<icache_sim_t> ic;
  std::unique_ptr<dcache_sim_t> dc;
  std::unique_ptr<cache_sim_t> l2;

  for (;;) {
    static struct option long_opts[] = {
      { "ic", required_argument, 0, 0 },
      { "dc", required_argument, 0, 0 },
      { "l2", required_argument, 0, 0 },
      { NULL, 0, 0, 0 }
    };
    int long_optind;
    int c;

    c = getopt_long(argc, argv, "p:m:dt", long_opts, &long_optind);
    if (c == -1)
      break;

    switch (c) {
      case 0:
        switch (long_optind) {
          case 0:
            ic.reset(new icache_sim_t(optarg));
            break;
          case 1:
            dc.reset(new dcache_sim_t(optarg));
            break;
          default:
            l2.reset(cache_sim_t::construct(optarg, "L2$"));
            break;
        }
        break;
      case 'p':
        nprocs = strtoul(optarg, NULL, 10);
        break;
      case 'm':
        mem_mb = strtoul(optarg, NULL, 10);
        break;
      case 'd':
        debug = true;
        /* fall-through */
      case 't':
        host_pty = true;
        break;
      default:
        help();
    }
  }
  if (optind >= argc) {
    fprintf(stderr, "%s: missing target operand\n", argv[0]);
    help();
  }

  const char* const* c_argv = argv;
  std::vector<std::string> htif_args(c_argv + optind, c_argv + argc);
  sim_t s(nprocs, mem_mb, htif_args, host_pty);

  if (ic && l2) ic->set_miss_handler(&*l2);
  if (dc && l2) dc->set_miss_handler(&*l2);
  for (unsigned int i = nprocs; i > 0; i--)
  {
    if (ic) s.get_core(i)->get_mmu()->register_memtracer(&*ic);
    if (dc) s.get_core(i)->get_mmu()->register_memtracer(&*dc);
  }

  s.run(debug);
  return (EXIT_SUCCESS);
}
