
/*
 * The use of this software is limited to education, research, and evaluation
 * purposes only.  Commercial use is strictly prohibited.  For all other uses,
 * contact the author(s).
 * Copyright(c) 2018 Souta Kawahara
 * Copyright(c) 2018 Hiroki Shirokura
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <vector>
#include <iostream>
#include <fstream>
#include "xellico.h"
#include "dpdk_misc.h"
#include "lcore_conf.h"
#include "forwarder.h"
#include "force_quit.h"
#include "port.h"
#include "delay.h"
#include "json.hpp"

std::vector <struct queue_conf> all_qconf;
struct lcore_conf lcore_conf[RTE_MAX_LCORE];
size_t tx_buffer_size;

static void
create_lcore_conf (void)
{
  const size_t n_lcores = rte_lcore_count ();
  for (size_t i=0; i<n_lcores; i++)
    {
      for (size_t j=0; j<all_qconf.size(); j++)
        if (all_qconf[j].lcore_id == i)
          lcore_conf[i].qconf.push_back (all_qconf[j]);
    }


  for (size_t i=0; i<n_lcores; i++)
    {
      const size_t n_ports = rte_eth_dev_count ();
      for (size_t pid=0; pid<n_ports; pid++)
        {
          struct rte_eth_dev_tx_buffer* txbuff = (struct rte_eth_dev_tx_buffer*)
            rte_zmalloc_socket ("tx_buffer",
              RTE_ETH_TX_BUFFER_SIZE (MAX_PKT_BURST), 0,
              rte_eth_dev_socket_id (pid));
          if (txbuff == NULL)
            rte_exit(EXIT_FAILURE, "Cannot allocate buffer for tx on port %u\n",
                (unsigned) pid);

          rte_eth_tx_buffer_init (txbuff, tx_buffer_size);
          lcore_conf[i].tx_buffer[pid] = txbuff;
        }
    }
}

static void
read_conf (const char* configfile)
{
  std::ifstream f (configfile);
  nlohmann::json json;
  f >> json;

  const size_t n_qconf = json["qconf"].size ();
  for (size_t i=0; i<n_qconf; i++)
    {
      auto ele = json["qconf"][i];
      auto port_id = ele["port_id"].get<uint32_t> ();
      auto queue_id = ele["queue_id"].get<uint32_t> ();
      auto lcore_id = ele["lcore_id"].get<uint32_t> ();
      printf ("(%u,%u,%u)\n", port_id, queue_id, lcore_id);

      all_qconf.push_back ({port_id, queue_id, lcore_id});
    }
  tx_buffer_size = json["txbulk"].get<size_t> ();
  printf ("tx_buffer_size: %zd\n", tx_buffer_size);
}

static void
validate_conf (void)
{
  // TODO Impelmentation
  return ;
}

static int
launcher (__attribute__ ((unused)) void *dummy)
{
  forwarder ();
  return 0;
}

static void
signal_handler (int signum)
{
  if (signum == SIGINT || signum == SIGTERM)
    {
      printf("\n\nSignal %d received, preparing to exit...\n", signum);
      force_quit = true;
    }
}

int
main (int argc, char **argv)
{
  int ret = xellico_boot_dpdk (argc, argv);
  argc -= ret;
  argv += ret;

  force_quit = false;
  signal (SIGINT, signal_handler);
  signal (SIGTERM, signal_handler);

  read_conf ("config.json");
  validate_conf ();
  create_lcore_conf ();
  port_init ();
  init_fib ();

  rte_eal_mp_remote_launch (launcher, NULL, SKIP_MASTER);
  rte_eal_mp_wait_lcore ();

  port_fini ();
  printf ("Bye...\n");
  return ret;
}


