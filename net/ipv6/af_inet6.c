/*
 *	PF_INET6 socket protocol family
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	Adapted from linux/net/ipv4/af_inet.c
 *
 *	$Id: af_inet6.c,v 1.66 2002/02/01 22:01:04 davem Exp $
 *
 * 	Fixes:
 *	piggy, Karl Knutson	:	Socket protocol table
 * 	Hideaki YOSHIFUJI	:	sin6_scope_id support
 * 	Arnaldo Melo		: 	check proc_net_create return, cleanups
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */


#include <linux/module.h>
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/version.h>

#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/icmpv6.h>
#include <linux/brlock.h>
#include <linux/smp_lock.h>

#include <net/ip.h>
#include <net/ipv6.h>
#include <net/udp.h>
#include <net/tcp.h>
#include <net/ipip.h>
#include <net/protocol.h>
#include <net/inet_common.h>
#include <net/transp_v6.h>
#include <net/ip6_route.h>
#include <net/addrconf.h>

#include <asm/uaccess.h>
#include <asm/system.h>

#ifdef MODULE
static int unloadable = 0; /* XX: Turn to one when all is ok within the
			      module for allowing unload */
#endif

MODULE_AUTHOR("Cast of dozens");
MODULE_DESCRIPTION("IPv6 protocol stack for Linux");
MODULE_PARM(unloadable, "i");

/* IPv6 procfs goodies... */

#ifdef CONFIG_PROC_FS
extern int anycast6_get_info(char *, char **, off_t, int);
extern int raw6_get_info(char *, char **, off_t, int);
extern int tcp6_get_info(char *, char **, off_t, int);
extern int udp6_get_info(char *, char **, off_t, int);
extern int afinet6_get_info(char *, char **, off_t, int);
extern int afinet6_get_snmp(char *, char **, off_t, int);
#endif

#ifdef CONFIG_SYSCTL
extern void ipv6_sysctl_register(void);
extern void ipv6_sysctl_unregister(void);
#endif

int sysctl_ipv6_bindv6only;

#ifdef INET_REFCNT_DEBUG
atomic_t inet6_sock_nr;
#endif

/* Per protocol sock slabcache */
kmem_cache_t *tcp6_sk_cachep;
kmem_cache_t *udp6_sk_cachep;
kmem_cache_t *raw6_sk_cachep;

/* The inetsw table contains everything that inet_create needs to
 * build a new socket.
 */
struct list_head inetsw6[SOCK_MAX];

static void inet6_sock_destruct(struct sock *sk)
{
	inet_sock_destruct(sk);

#ifdef INET_REFCNT_DEBUG
	atomic_dec(&inet6_sock_nr);
#endif
	MOD_DEC_USE_COUNT;
}

static __inline__ kmem_cache_t *inet6_sk_slab(int protocol)
{
        kmem_cache_t* rc = tcp6_sk_cachep;

        if (protocol == IPPROTO_UDP)
                rc = udp6_sk_cachep;
        else if (protocol == IPPROTO_RAW)
                rc = raw6_sk_cachep;
        return rc;
}

static __inline__ int inet6_sk_size(int protocol)
{
        int rc = sizeof(struct tcp6_sock);

        if (protocol == IPPROTO_UDP)
                rc = sizeof(struct udp6_sock);
        else if (protocol == IPPROTO_RAW)
                rc = sizeof(struct raw6_sock);
        return rc;
}

static __inline__ struct ipv6_pinfo *inet6_sk_generic(struct sock *sk)
{
	struct ipv6_pinfo *rc = (&((struct tcp6_sock *)sk)->inet6);

        if (sk->protocol == IPPROTO_UDP)
                rc = (&((struct udp6_sock *)sk)->inet6);
        else if (sk->protocol == IPPROTO_RAW)
                rc = (&((struct raw6_sock *)sk)->inet6);
        return rc;
}

static int inet6_create(struct socket *sock, int protocol)
{
	struct inet_opt *inet;
	struct ipv6_pinfo *np;
	struct sock *sk;
	struct tcp6_sock* tcp6sk;
	struct list_head *p;
	struct inet_protosw *answer;

	sk = sk_alloc(PF_INET6, GFP_KERNEL, inet6_sk_size(protocol),
		      inet6_sk_slab(protocol));
	if (sk == NULL) 
		goto do_oom;

	/* Look for the requested type/protocol pair. */
	answer = NULL;
	br_read_lock_bh(BR_NETPROTO_LOCK);
	list_for_each(p, &inetsw6[sock->type]) {
		answer = list_entry(p, struct inet_protosw, list);

		/* Check the non-wild match. */
		if (protocol == answer->protocol) {
			if (protocol != IPPROTO_IP)
				break;
		} else {
			/* Check for the two wild cases. */
			if (IPPROTO_IP == protocol) {
				protocol = answer->protocol;
				break;
			}
			if (IPPROTO_IP == answer->protocol)
				break;
		}
		answer = NULL;
	}
	br_read_unlock_bh(BR_NETPROTO_LOCK);

	if (!answer)
		goto free_and_badtype;
	if (answer->capability > 0 && !capable(answer->capability))
		goto free_and_badperm;
	if (!protocol)
		goto free_and_noproto;

	sock->ops = answer->ops;
	sock_init_data(sock, sk);

	sk->prot = answer->prot;
	sk->no_check = answer->no_check;
	if (INET_PROTOSW_REUSE & answer->flags)
		sk->reuse = 1;

	inet = inet_sk(sk);

	if (SOCK_RAW == sock->type) {
		inet->num = protocol;
		if (IPPROTO_RAW == protocol)
			inet->hdrincl = 1;
	}

	sk->destruct            = inet6_sock_destruct;
	sk->zapped		= 0;
	sk->family		= PF_INET6;
	sk->protocol		= protocol;

	sk->backlog_rcv		= answer->prot->backlog_rcv;

	tcp6sk		= (struct tcp6_sock *)sk;
	tcp6sk->pinet6 = np = inet6_sk_generic(sk);
	np->hop_limit	= -1;
	np->mcast_hops	= -1;
	np->mc_loop	= 1;
	np->pmtudisc	= IPV6_PMTUDISC_WANT;
	np->ipv6only	= sysctl_ipv6_bindv6only;
	
	/* Init the ipv4 part of the socket since we can have sockets
	 * using v6 API for ipv4.
	 */
	inet->ttl	= 64;

	inet->mc_loop	= 1;
	inet->mc_ttl	= 1;
	inet->mc_index	= 0;
	inet->mc_list	= NULL;

	if (ipv4_config.no_pmtu_disc)
		inet->pmtudisc = IP_PMTUDISC_DONT;
	else
		inet->pmtudisc = IP_PMTUDISC_WANT;


#ifdef INET_REFCNT_DEBUG
	atomic_inc(&inet6_sock_nr);
	atomic_inc(&inet_sock_nr);
#endif
	MOD_INC_USE_COUNT;

	if (inet->num) {
		/* It assumes that any protocol which allows
		 * the user to assign a number at socket
		 * creation time automatically shares.
		 */
		inet->sport = ntohs(inet->num);
		sk->prot->hash(sk);
	}
	if (sk->prot->init) {
		int err = sk->prot->init(sk);
		if (err != 0) {
			MOD_DEC_USE_COUNT;
			inet_sock_release(sk);
			return err;
		}
	}
	return 0;

free_and_badtype:
	sk_free(sk);
	return -ESOCKTNOSUPPORT;
free_and_badperm:
	sk_free(sk);
	return -EPERM;
free_and_noproto:
	sk_free(sk);
	return -EPROTONOSUPPORT;
do_oom:
	return -ENOBUFS;
}


/* bind for INET6 API */
int inet6_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sockaddr_in6 *addr=(struct sockaddr_in6 *)uaddr;
	struct sock *sk = sock->sk;
	struct inet_opt *inet = inet_sk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);
	__u32 v4addr = 0;
	unsigned short snum;
	int addr_type = 0;

	/* If the socket has its own bind function then use it. */
	if(sk->prot->bind)
		return sk->prot->bind(sk, uaddr, addr_len);

	if (addr_len < SIN6_LEN_RFC2133)
		return -EINVAL;
	addr_type = ipv6_addr_type(&addr->sin6_addr);
	if ((addr_type & IPV6_ADDR_MULTICAST) && sock->type == SOCK_STREAM)
		return -EINVAL;

	/* Check if the address belongs to the host. */
	if (addr_type == IPV6_ADDR_MAPPED) {
		v4addr = addr->sin6_addr.s6_addr32[3];
		if (inet_addr_type(v4addr) != RTN_LOCAL)
			return -EADDRNOTAVAIL;
	} else {
		if (addr_type != IPV6_ADDR_ANY) {
			/* ipv4 addr of the socket is invalid.  Only the
			 * unpecified and mapped address have a v4 equivalent.
			 */
			v4addr = LOOPBACK4_IPV6;
			if (!(addr_type & IPV6_ADDR_MULTICAST))	{
				if (!ipv6_chk_addr(&addr->sin6_addr, NULL))
					return -EADDRNOTAVAIL;
			}
		}
	}

	snum = ntohs(addr->sin6_port);
	if (snum && snum < PROT_SOCK && !capable(CAP_NET_BIND_SERVICE))
		return -EACCES;

	lock_sock(sk);

	/* Check these errors (active socket, double bind). */
	if (sk->state != TCP_CLOSE || inet->num) {
		release_sock(sk);
		return -EINVAL;
	}

	if (addr_type & IPV6_ADDR_LINKLOCAL) {
		if (addr_len >= sizeof(struct sockaddr_in6) &&
		    addr->sin6_scope_id) {
			/* Override any existing binding, if another one
			 * is supplied by user.
			 */
			sk->bound_dev_if = addr->sin6_scope_id;
		}

		/* Binding to link-local address requires an interface */
		if (sk->bound_dev_if == 0) {
			release_sock(sk);
			return -EINVAL;
		}
	}

	inet->rcv_saddr = v4addr;
	inet->saddr = v4addr;

	ipv6_addr_copy(&np->rcv_saddr, &addr->sin6_addr);
		
	if (!(addr_type & IPV6_ADDR_MULTICAST))
		ipv6_addr_copy(&np->saddr, &addr->sin6_addr);

	/* Make sure we are allowed to bind here. */
	if (sk->prot->get_port(sk, snum) != 0) {
		inet->rcv_saddr = inet->saddr = 0;
		memset(&np->rcv_saddr, 0, sizeof(struct in6_addr));
		memset(&np->saddr, 0, sizeof(struct in6_addr));

		release_sock(sk);
		return -EADDRINUSE;
	}

	if (addr_type != IPV6_ADDR_ANY)
		sk->userlocks |= SOCK_BINDADDR_LOCK;
	if (snum)
		sk->userlocks |= SOCK_BINDPORT_LOCK;
	inet->sport = ntohs(inet->num);
	inet->dport = 0;
	inet->daddr = 0;
	release_sock(sk);

	return 0;
}

int inet6_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

	if (sk == NULL)
		return -EINVAL;

	/* Free mc lists */
	ipv6_sock_mc_close(sk);

	/* Free ac lists */
	ipv6_sock_ac_close(sk);

	return inet_release(sock);
}

int inet6_destroy_sock(struct sock *sk)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct sk_buff *skb;
	struct ipv6_txoptions *opt;

	/*
	 *	Release destination entry
	 */

	sk_dst_reset(sk);

	/* Release rx options */

	if ((skb = xchg(&np->pktoptions, NULL)) != NULL)
		kfree_skb(skb);

	/* Free flowlabels */
	fl6_free_socklist(sk);

	/* Free tx options */

	if ((opt = xchg(&np->opt, NULL)) != NULL)
		sock_kfree_s(sk, opt, opt->tot_len);

	return 0;
}

/*
 *	This does both peername and sockname.
 */
 
int inet6_getname(struct socket *sock, struct sockaddr *uaddr,
		 int *uaddr_len, int peer)
{
	struct sockaddr_in6 *sin=(struct sockaddr_in6 *)uaddr;
	struct sock *sk = sock->sk;
	struct inet_opt *inet = inet_sk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);
  
	sin->sin6_family = AF_INET6;
	sin->sin6_flowinfo = 0;
	sin->sin6_scope_id = 0;
	if (peer) {
		if (!inet->dport)
			return -ENOTCONN;
		if (((1<<sk->state)&(TCPF_CLOSE|TCPF_SYN_SENT)) && peer == 1)
			return -ENOTCONN;
		sin->sin6_port = inet->dport;
		memcpy(&sin->sin6_addr, &np->daddr, sizeof(struct in6_addr));
		if (np->sndflow)
			sin->sin6_flowinfo = np->flow_label;
	} else {
		if (ipv6_addr_type(&np->rcv_saddr) == IPV6_ADDR_ANY)
			memcpy(&sin->sin6_addr, &np->saddr,
			       sizeof(struct in6_addr));
		else
			memcpy(&sin->sin6_addr, &np->rcv_saddr,
			       sizeof(struct in6_addr));

		sin->sin6_port = inet->sport;
	}
	if (ipv6_addr_type(&sin->sin6_addr) & IPV6_ADDR_LINKLOCAL)
		sin->sin6_scope_id = sk->bound_dev_if;
	*uaddr_len = sizeof(*sin);
	return(0);
}

int inet6_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = sock->sk;
	int err = -EINVAL;

	switch(cmd) 
	{
	case SIOCGSTAMP:
		if(sk->stamp.tv_sec==0)
			return -ENOENT;
		err = copy_to_user((void *)arg, &sk->stamp,
				   sizeof(struct timeval));
		if (err)
			return -EFAULT;
		return 0;

	case SIOCADDRT:
	case SIOCDELRT:
	  
		return(ipv6_route_ioctl(cmd,(void *)arg));

	case SIOCSIFADDR:
		return addrconf_add_ifaddr((void *) arg);
	case SIOCDIFADDR:
		return addrconf_del_ifaddr((void *) arg);
	case SIOCSIFDSTADDR:
		return addrconf_set_dstaddr((void *) arg);
	default:
		if(sk->prot->ioctl==0 || (err=sk->prot->ioctl(sk, cmd, arg))==-ENOIOCTLCMD)
			return(dev_ioctl(cmd,(void *) arg));		
		return err;
	}
	/*NOTREACHED*/
	return(0);
}

struct proto_ops inet6_stream_ops = {
	.family =	PF_INET6,

	.release =	inet6_release,
	.bind =		inet6_bind,
	.connect =	inet_stream_connect,		/* ok		*/
	.socketpair =	sock_no_socketpair,		/* a do nothing	*/
	.accept =	inet_accept,			/* ok		*/
	.getname =	inet6_getname, 
	.poll =		tcp_poll,			/* ok		*/
	.ioctl =	inet6_ioctl,			/* must change  */
	.listen =	inet_listen,			/* ok		*/
	.shutdown =	inet_shutdown,			/* ok		*/
	.setsockopt =	inet_setsockopt,		/* ok		*/
	.getsockopt =	inet_getsockopt,		/* ok		*/
	.sendmsg =	inet_sendmsg,			/* ok		*/
	.recvmsg =	inet_recvmsg,			/* ok		*/
	.mmap =		sock_no_mmap,
	.sendpage =	tcp_sendpage
};

struct proto_ops inet6_dgram_ops = {
	.family =	PF_INET6,

	.release =	inet6_release,
	.bind =		inet6_bind,
	.connect =	inet_dgram_connect,		/* ok		*/
	.socketpair =	sock_no_socketpair,		/* a do nothing	*/
	.accept =	sock_no_accept,			/* a do nothing	*/
	.getname =	inet6_getname, 
	.poll =		datagram_poll,			/* ok		*/
	.ioctl =	inet6_ioctl,			/* must change  */
	.listen =	sock_no_listen,			/* ok		*/
	.shutdown =	inet_shutdown,			/* ok		*/
	.setsockopt =	inet_setsockopt,		/* ok		*/
	.getsockopt =	inet_getsockopt,		/* ok		*/
	.sendmsg =	inet_sendmsg,			/* ok		*/
	.recvmsg =	inet_recvmsg,			/* ok		*/
	.mmap =		sock_no_mmap,
	.sendpage =	sock_no_sendpage,
};

struct net_proto_family inet6_family_ops = {
	.family =PF_INET6,
	.create =inet6_create,
};

#ifdef MODULE
#if 0 /* FIXME --RR */
int ipv6_unload(void)
{
	if (!unloadable) return 1;
	/* We keep internally 3 raw sockets */
	return atomic_read(&(__this_module.uc.usecount)) - 3;
}
#endif
#endif

#if defined(MODULE) && defined(CONFIG_SYSCTL)
extern void ipv6_sysctl_register(void);
extern void ipv6_sysctl_unregister(void);
#endif

static struct inet_protosw rawv6_protosw = {
	.type =      SOCK_RAW,
	.protocol =  IPPROTO_IP,	/* wild card */
	.prot =      &rawv6_prot,
	.ops =       &inet6_dgram_ops,
	.capability =CAP_NET_RAW,
	.no_check =  UDP_CSUM_DEFAULT,
	.flags =     INET_PROTOSW_REUSE,
};

#define INETSW6_ARRAY_LEN (sizeof(inetsw6_array) / sizeof(struct inet_protosw))

void
inet6_register_protosw(struct inet_protosw *p)
{
	struct list_head *lh;
	struct inet_protosw *answer;
	int protocol = p->protocol;
	struct list_head *last_perm;

	br_write_lock_bh(BR_NETPROTO_LOCK);

	if (p->type > SOCK_MAX)
		goto out_illegal;

	/* If we are trying to override a permanent protocol, bail. */
	answer = NULL;
	last_perm = &inetsw6[p->type];
	list_for_each(lh, &inetsw6[p->type]) {
		answer = list_entry(lh, struct inet_protosw, list);

		/* Check only the non-wild match. */
		if (INET_PROTOSW_PERMANENT & answer->flags) {
			if (protocol == answer->protocol)
				break;
			last_perm = lh;
		}

		answer = NULL;
	}
	if (answer)
		goto out_permanent;

	/* Add the new entry after the last permanent entry if any, so that
	 * the new entry does not override a permanent entry when matched with
	 * a wild-card protocol. But it is allowed to override any existing
	 * non-permanent entry.  This means that when we remove this entry, the 
	 * system automatically returns to the old behavior.
	 */
	list_add(&p->list, last_perm);
out:
	br_write_unlock_bh(BR_NETPROTO_LOCK);
	return;

out_permanent:
	printk(KERN_ERR "Attempt to override permanent protocol %d.\n",
	       protocol);
	goto out;

out_illegal:
	printk(KERN_ERR
	       "Ignoring attempt to register illegal socket type %d.\n",
	       p->type);
	goto out;
}

void
inet6_unregister_protosw(struct inet_protosw *p)
{
	inet_unregister_protosw(p);
}

static int __init init_ipv6_mibs(void)
{
	int i;
 
	ipv6_statistics[0] = kmalloc_percpu(sizeof (struct ipv6_mib),
						GFP_KERNEL);
	if (!ipv6_statistics[0])
		goto err_ip_mib0;
	ipv6_statistics[1] = kmalloc_percpu(sizeof (struct ipv6_mib),
						GFP_KERNEL);
	if (!ipv6_statistics[1])
		goto err_ip_mib1;
	
	icmpv6_statistics[0] = kmalloc_percpu(sizeof (struct icmpv6_mib),
						GFP_KERNEL);
	if (!icmpv6_statistics[0])
		goto err_icmp_mib0;
	icmpv6_statistics[1] = kmalloc_percpu(sizeof (struct icmpv6_mib),
						GFP_KERNEL);
	if (!icmpv6_statistics[1])
		goto err_icmp_mib1;
	
	udp_stats_in6[0] = kmalloc_percpu(sizeof (struct udp_mib),
						GFP_KERNEL);
	if (!udp_stats_in6[0])
		goto err_udp_mib0;
	udp_stats_in6[1] = kmalloc_percpu(sizeof (struct udp_mib),
						GFP_KERNEL);
	if (!udp_stats_in6[1])
		goto err_udp_mib1;

	/* Zero all percpu versions of the mibs */
	for (i = 0; i < NR_CPUS; i++) {
		if (cpu_possible(i)) {
		memset(per_cpu_ptr(ipv6_statistics[0], i), 0,
				sizeof (struct ipv6_mib));
		memset(per_cpu_ptr(ipv6_statistics[1], i), 0,
				sizeof (struct ipv6_mib));
		memset(per_cpu_ptr(icmpv6_statistics[0], i), 0,
				sizeof (struct icmpv6_mib));
		memset(per_cpu_ptr(icmpv6_statistics[1], i), 0,
				sizeof (struct icmpv6_mib));
		memset(per_cpu_ptr(udp_stats_in6[0], i), 0,
				sizeof (struct udp_mib));
		memset(per_cpu_ptr(udp_stats_in6[1], i), 0,
				sizeof (struct udp_mib));
		}
	}
	return 0;

err_udp_mib1:
	kfree_percpu(udp_stats_in6[0]);
err_udp_mib0:
	kfree_percpu(icmpv6_statistics[1]);
err_icmp_mib1:
	kfree_percpu(icmpv6_statistics[0]);
err_icmp_mib0:
	kfree_percpu(ipv6_statistics[1]);
err_ip_mib1:
	kfree_percpu(ipv6_statistics[0]);
err_ip_mib0:
	return -ENOMEM;
	
}

static void cleanup_ipv6_mibs(void)
{
	kfree_percpu(ipv6_statistics[0]);
	kfree_percpu(ipv6_statistics[1]);
	kfree_percpu(icmpv6_statistics[0]);
	kfree_percpu(icmpv6_statistics[1]);
	kfree_percpu(udp_stats_in6[0]);
	kfree_percpu(udp_stats_in6[1]);
}
	
static int __init inet6_init(void)
{
	struct sk_buff *dummy_skb;
        struct list_head *r;
	int err;

#ifdef MODULE
#if 0 /* FIXME --RR */
	if (!mod_member_present(&__this_module, can_unload))
	  return -EINVAL;

	__this_module.can_unload = &ipv6_unload;
#endif
#endif

	printk(KERN_INFO "IPv6 v0.8 for NET4.0\n");

	if (sizeof(struct inet6_skb_parm) > sizeof(dummy_skb->cb))
	{
		printk(KERN_CRIT "inet6_proto_init: size fault\n");
		return -EINVAL;
	}
	/* allocate our sock slab caches */
        tcp6_sk_cachep = kmem_cache_create("tcp6_sock",
					   sizeof(struct tcp6_sock), 0,
                                           SLAB_HWCACHE_ALIGN, 0, 0);
        udp6_sk_cachep = kmem_cache_create("udp6_sock",
					   sizeof(struct udp6_sock), 0,
                                           SLAB_HWCACHE_ALIGN, 0, 0);
        raw6_sk_cachep = kmem_cache_create("raw6_sock",
					   sizeof(struct raw6_sock), 0,
                                           SLAB_HWCACHE_ALIGN, 0, 0);
        if (!tcp6_sk_cachep || !udp6_sk_cachep || !raw6_sk_cachep)
                printk(KERN_CRIT "%s: Can't create protocol sock SLAB "
		       "caches!\n", __FUNCTION__);

	/* Register the socket-side information for inet6_create.  */
	for(r = &inetsw6[0]; r < &inetsw6[SOCK_MAX]; ++r)
		INIT_LIST_HEAD(r);

	/* We MUST register RAW sockets before we create the ICMP6,
	 * IGMP6, or NDISC control sockets.
	 */
	inet6_register_protosw(&rawv6_protosw);

	/* Register the family here so that the init calls below will
	 * be able to create sockets. (?? is this dangerous ??)
	 */
	(void) sock_register(&inet6_family_ops);

	/* Initialise ipv6 mibs */
	err = init_ipv6_mibs();
	if (err)
		goto init_mib_fail;
	
	/*
	 *	ipngwg API draft makes clear that the correct semantics
	 *	for TCP and UDP is to consider one TCP and UDP instance
	 *	in a host availiable by both INET and INET6 APIs and
	 *	able to communicate via both network protocols.
	 */

#if defined(MODULE) && defined(CONFIG_SYSCTL)
	ipv6_sysctl_register();
#endif
	err = icmpv6_init(&inet6_family_ops);
	if (err)
		goto icmp_fail;
	err = ndisc_init(&inet6_family_ops);
	if (err)
		goto ndisc_fail;
	err = igmp6_init(&inet6_family_ops);
	if (err)
		goto igmp_fail;
	/* Create /proc/foo6 entries. */
#ifdef CONFIG_PROC_FS
	err = -ENOMEM;
	if (!proc_net_create("raw6", 0, raw6_get_info))
		goto proc_raw6_fail;
	if (!proc_net_create("tcp6", 0, tcp6_get_info))
		goto proc_tcp6_fail;
	if (!proc_net_create("udp6", 0, udp6_get_info))
		goto proc_udp6_fail;
	if (!proc_net_create("sockstat6", 0, afinet6_get_info))
		goto proc_sockstat6_fail;
	if (!proc_net_create("snmp6", 0, afinet6_get_snmp))
		goto proc_snmp6_fail;
	if (!proc_net_create("anycast6", 0, anycast6_get_info))
		goto proc_anycast6_fail;
#endif
	ipv6_netdev_notif_init();
	ipv6_packet_init();
	ip6_route_init();
	ip6_flowlabel_init();
	addrconf_init();
	sit_init();

	/* Init v6 extention headers. */
	ipv6_hopopts_init();
	ipv6_rthdr_init();
	ipv6_frag_init();
	ipv6_nodata_init();
	ipv6_destopt_init();

	/* Init v6 transport protocols. */
	udpv6_init();
	tcpv6_init();

	return 0;

#ifdef CONFIG_PROC_FS
proc_anycast6_fail:
	proc_net_remove("anycast6");
proc_snmp6_fail:
	proc_net_remove("sockstat6");
proc_sockstat6_fail:
	proc_net_remove("udp6");
proc_udp6_fail:
	proc_net_remove("tcp6");
proc_tcp6_fail:
        proc_net_remove("raw6");
proc_raw6_fail:
	igmp6_cleanup();
#endif
igmp_fail:
	ndisc_cleanup();
ndisc_fail:
	icmpv6_cleanup();
icmp_fail:
#if defined(MODULE) && defined(CONFIG_SYSCTL)
	ipv6_sysctl_unregister();
#endif
	cleanup_ipv6_mibs();
init_mib_fail:
	return err;
}
module_init(inet6_init);


#ifdef MODULE
static void inet6_exit(void)
{
	/* First of all disallow new sockets creation. */
	sock_unregister(PF_INET6);
#ifdef CONFIG_PROC_FS
	proc_net_remove("raw6");
	proc_net_remove("tcp6");
	proc_net_remove("udp6");
	proc_net_remove("sockstat6");
	proc_net_remove("snmp6");
	proc_net_remove("anycast6");
#endif
	/* Cleanup code parts. */
	sit_cleanup();
	ipv6_netdev_notif_cleanup();
	ip6_flowlabel_cleanup();
	addrconf_cleanup();
	ip6_route_cleanup();
	ipv6_packet_cleanup();
	igmp6_cleanup();
	ndisc_cleanup();
	icmpv6_cleanup();
#ifdef CONFIG_SYSCTL
	ipv6_sysctl_unregister();	
#endif
	cleanup_ipv6_mibs();
}
module_exit(inet6_exit);
#endif /* MODULE */
MODULE_LICENSE("GPL");
