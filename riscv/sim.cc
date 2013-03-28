// See LICENSE for license details.

#include "sim.h"
#include "htif.h"
#include <sys/mman.h>
#include <map>
#include <iostream>
#include <climits>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#ifdef __linux__
# define mmap mmap64
#endif

sim_t::sim_t(int _nprocs, int mem_mb, const std::vector<std::string>& args, bool host_pty)
  : procs(_nprocs)
{
  // allocate target machine's memory, shrinking it as necessary
  // until the allocation succeeds
  size_t memsz0 = (size_t)mem_mb << 20;
  if (memsz0 == 0)
    memsz0 = 1L << (sizeof(size_t) == 8 ? 32 : 30);

  size_t quantum = std::max(PGSIZE, (reg_t)sysconf(_SC_PAGESIZE));
  memsz0 = memsz0/quantum*quantum;

  memsz = memsz0;
  mem = (char*)mmap(NULL, memsz, PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);

  if(mem == MAP_FAILED)
  {
    while(mem == MAP_FAILED && (memsz = memsz*10/11/quantum*quantum))
      mem = (char*)mmap(NULL, memsz, PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    assert(mem != MAP_FAILED);
    fprintf(stderr, "warning: only got %lu bytes of target mem (wanted %lu)\n",
            (unsigned long)memsz, (unsigned long)memsz0);
  }

  mmu = new mmu_t(mem, memsz);

  for(size_t i = 0; i < num_cores(); i++)
    procs[i] = new processor_t(this, new mmu_t(mem, memsz), i);

  int host_in = STDIN_FILENO;
  int host_out = STDOUT_FILENO;

  if (host_pty) {
    int pty = posix_openpt(O_RDWR | O_NOCTTY);
    if (pty < 0 || (grantpt(pty) != 0) || (unlockpt(pty) != 0)) {
      perror("failed to allocate pty");
    } else {
      host_in = host_out = pty;

      /* Induce a HUP condition */
      char *pts = ptsname(pty);
      int fd = open(pts, O_RDWR | O_NOCTTY);
      close(fd);

      fprintf(stderr, "pty allocated: %s\n", pts);
      /* Poll until HUP condition vanishes, which should
         indicate that the slave device has been (re)opened */
      for (;;)
      {
        struct pollfd fds;
        fds.fd = pty;
        fds.events = POLLHUP;
        if (poll(&fds, 1, 0) < 0) {
          perror("poll");
          break;
        }
        if (!(fds.revents & POLLHUP))
          break;
        usleep(100);
      }
    }
  }
  htif = new htif_isasim_t(this, args, host_in, host_out);
}

sim_t::~sim_t()
{
  for(size_t i = 0; i < num_cores(); i++)
  {
    mmu_t* pmmu = &procs[i]->mmu;
    delete procs[i];
    delete pmmu;
  }
  delete mmu;
  munmap(mem, memsz);
}

void sim_t::send_ipi(reg_t who)
{
  if(who < num_cores())
    procs[who]->deliver_ipi();
}

reg_t sim_t::get_scr(int which)
{
  switch (which)
  {
    case 0: return num_cores();
    case 1: return memsz >> 20;
    default: return -1;
  }
}

void sim_t::run(bool debug)
{
  while (!htif->done())
  {
    if(!debug)
      step_all(10000, 1000, false);
    else
      interactive();
  }
}

void sim_t::step_all(size_t n, size_t interleave, bool noisy)
{
  htif->tick();
  for(size_t j = 0; j < n; j+=interleave)
  {
    for(int i = 0; i < (int)num_cores(); i++)
      procs[i]->step(interleave,noisy);
  }
}
