
#ifndef LORA_GW_PPP
#define LORA_GW_PPP

#define PPP_SUPPORT 1
#define PPPOL2TP_SUPPORT 1
#define CONFIG_LWIP_PPP_SUPPORT 1
#define PPP_AUTH_SUPPORT 1
#define PPPAUTHTYPE_ANY       0xff

#include <lwip/raw.h>
#include <lwip/netif.h>
#include <netif/ppp/ppp.h>
#include <netif/ppp/pppol2tp.h>

/* The PPP IP interface */
struct netif ppp_netif;
/* The PPP control block */
ppp_pcb *ppp;

void status_cb(ppp_pcb *pcb, int err_code, void *ctx) {
  struct netif *pppif = ppp_netif(pcb);
  LWIP_UNUSED_ARG(ctx);

  switch(err_code) {
    case PPPERR_NONE: {
#if LWIP_DNS
      const ip_addr_t *ns;
#endif /* LWIP_DNS */
      printf("status_cb: Connected\n");
#if PPP_IPV4_SUPPORT
      printf("   our_ipaddr  = %s\n", ipaddr_ntoa(&pppif->ip_addr));
      printf("   his_ipaddr  = %s\n", ipaddr_ntoa(&pppif->gw));
      printf("   netmask     = %s\n", ipaddr_ntoa(&pppif->netmask));
#if LWIP_DNS
      ns = dns_getserver(0);
      printf("   dns1        = %s\n", ipaddr_ntoa(ns));
      ns = dns_getserver(1);
      printf("   dns2        = %s\n", ipaddr_ntoa(ns));
#endif /* LWIP_DNS */
#endif /* PPP_IPV4_SUPPORT */
#if PPP_IPV6_SUPPORT
      printf("   our6_ipaddr = %s\n", ip6addr_ntoa(netif_ip6_addr(pppif, 0)));
#endif /* PPP_IPV6_SUPPORT */
      break;
    }
    case PPPERR_PARAM: {
      printf("status_cb: Invalid parameter\n");
      break;
    }
    case PPPERR_OPEN: {
      printf("status_cb: Unable to open PPP session\n");
      break;
    }
    case PPPERR_DEVICE: {
      printf("status_cb: Invalid I/O device for PPP\n");
      break;
    }
    case PPPERR_ALLOC: {
      printf("status_cb: Unable to allocate resources\n");
      break;
    }
    case PPPERR_USER: {
      printf("status_cb: User interrupt\n");
      break;
    }
    case PPPERR_CONNECT: {
      printf("status_cb: Connection lost\n");
      break;
    }
    case PPPERR_AUTHFAIL: {
      printf("status_cb: Failed authentication challenge\n");
      break;
    }
    case PPPERR_PROTOCOL: {
      printf("status_cb: Failed to meet protocol\n");
      break;
    }
    case PPPERR_PEERDEAD: {
      printf("status_cb: Connection timeout\n");
      break;
    }
    case PPPERR_IDLETIMEOUT: {
      printf("status_cb: Idle Timeout\n");
      break;
    }
    case PPPERR_CONNECTTIME: {
      printf("status_cb: Max connect time reached\n");
      break;
    }
    case PPPERR_LOOPBACK: {
      printf("status_cb: Loopback detected\n");
      break;
    }
    default: {
      printf("status_cb: Unknown error code %d\n", err_code);
      break;
    }
  }

/*
 * This should be in the switch case, this is put outside of the switch
 * case for example readability.
 */

  if (err_code == PPPERR_NONE) {
    return;
  }

  /* ppp_close() was previously called, don't reconnect */
  if (err_code == PPPERR_USER) {
    /* ppp_free(); -- can be called here */
    return;
  }

  /*
   * Try to reconnect in 30 seconds, if you need a modem chatscript you have
   * to do a much better signaling here ;-)
   */
  ppp_connect(pcb, 30);
  /* OR ppp_listen(pcb); */
}

void startVPN() {
  ip4_addr_t addr;
  /* Set our address */
  IP4_ADDR(&addr, 192,168,0,1);
  ppp_set_ipcp_ouraddr(ppp, &addr);
  /* Set peer(his) address */
  IP4_ADDR(&addr, 192,168,0,2);
  ppp_set_ipcp_hisaddr(ppp, &addr);
  /* Set primary DNS server */
  IP4_ADDR(&addr, 192,168,10,20);
  ppp_set_ipcp_dnsaddr(ppp, 0, &addr);
  /* Set secondary DNS server */
  IP4_ADDR(&addr, 192,168,10,21);
  ppp_set_ipcp_dnsaddr(ppp, 1, &addr);
  /* Auth configuration, this is pretty self-explanatory */
  ppp_set_auth(ppp, PPPAUTHTYPE_ANY, "login", "password");
  /* Require peer to authenticate */
  ppp_set_auth_required(ppp, 1);
}
#endif