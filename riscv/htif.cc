// See LICENSE for license details.

#include "htif.h"
#include "sim.h"
#include <unistd.h>
#include <stdexcept>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <stddef.h>

htif_isasim_t::htif_isasim_t(sim_t* _sim, const std::vector<std::string>& args,
  int _host_in, int _host_out)
  : htif_pthread_t(args, _host_in, _host_out), sim(_sim), reset(true), seqno(1)
{
}

void htif_isasim_t::tick()
{
  do tick_once(); while (reset);
}

void htif_isasim_t::tick_once()
{
  packet_header_t hdr;
  recv(&hdr, sizeof(hdr));

  char buf[hdr.get_packet_size()];
  memcpy(buf, &hdr, sizeof(hdr));
  recv(buf + sizeof(hdr), hdr.get_payload_size());
  packet_t p(buf);

  assert(hdr.seqno == seqno);

  switch (hdr.cmd)
  {
    case HTIF_CMD_READ_MEM:
    {
      packet_header_t ack(HTIF_CMD_ACK, seqno, hdr.data_size, 0);
      send(&ack, sizeof(ack));

      uint64_t buf[hdr.data_size];
      for (size_t i = 0; i < hdr.data_size; i++)
        buf[i] = sim->mmu->load_uint64((hdr.addr+i)*HTIF_DATA_ALIGN);
      send(buf, hdr.data_size * sizeof(buf[0]));
      break;
    }
    case HTIF_CMD_WRITE_MEM:
    {
      const uint64_t* buf = (const uint64_t*)p.get_payload();
      for (size_t i = 0; i < hdr.data_size; i++)
        sim->mmu->store_uint64((hdr.addr+i)*HTIF_DATA_ALIGN, buf[i]);

      packet_header_t ack(HTIF_CMD_ACK, seqno, 0, 0);
      send(&ack, sizeof(ack));
      break;
    }
    case HTIF_CMD_READ_CONTROL_REG:
    case HTIF_CMD_WRITE_CONTROL_REG:
    {
      reg_t coreid = hdr.addr >> 20;
      reg_t regno = hdr.addr & ((1<<20)-1);
      assert(hdr.data_size == 1);

      packet_header_t ack(HTIF_CMD_ACK, seqno, 1, 0);
      send(&ack, sizeof(ack));

      if (coreid == 0xFFFFF) // system control register space
      {
        uint64_t scr = sim->get_scr(regno);
        send(&scr, sizeof(scr));
        break;
      }

      assert(coreid < sim->num_cores());
      uint64_t old_val = sim->procs[coreid]->get_pcr(regno);
      send(&old_val, sizeof(old_val));

      if (regno == PCR_TOHOST)
          sim->procs[coreid]->tohost = 0;

      if (hdr.cmd == HTIF_CMD_WRITE_CONTROL_REG)
      {
        uint64_t new_val;
        memcpy(&new_val, p.get_payload(), sizeof(new_val));
        if (regno == PCR_RESET)
        {
          if (reset && !(new_val & 1))
            reset = false;
          sim->procs[coreid]->reset(new_val & 1);
        }
        else if (regno == PCR_FROMHOST && old_val != 0)
          ; // ignore host writes to fromhost if target hasn't yet consumed
        else
          sim->procs[coreid]->set_pcr(regno, new_val);
      }
      break;
    }
    default:
      abort();
  }
  seqno++;
}

bool htif_isasim_t::done()
{
  if (reset)
    return false;
  for (size_t i = 0; i < sim->num_cores(); i++)
    if (sim->procs[i]->running())
      return false;
  return true;
}
