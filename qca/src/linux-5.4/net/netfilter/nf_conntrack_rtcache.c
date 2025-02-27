/* route cache for netfilter.
 *
 * (C) 2014 Red Hat GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/netfilter.h>
#include <linux/skbuff.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/export.h>
#include <linux/module.h>

#include <net/dst.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_conntrack_extend.h>
#include <net/netfilter/nf_conntrack_rtcache.h>

#if IS_ENABLED(CONFIG_NF_CONNTRACK_IPV6)
#include <net/ip6_fib.h>
#endif

static void __nf_conn_rtcache_destroy(struct nf_conn_rtcache *rtc,
				      enum ip_conntrack_dir dir)
{
	struct dst_entry *dst = xchg(&rtc->cached_dst[dir].dst, NULL);

	dst_release(dst);
	rtc->cached_dst[dir].iif = -1;
}

static void nf_conn_rtcache_destroy(struct nf_conn *ct)
{
	struct nf_conn_rtcache *rtc = nf_ct_rtcache_find(ct);

	if (!rtc)
		return;

	__nf_conn_rtcache_destroy(rtc, IP_CT_DIR_ORIGINAL);
	__nf_conn_rtcache_destroy(rtc, IP_CT_DIR_REPLY);
}

static void nf_ct_rtcache_ext_add(struct nf_conn *ct)
{
	struct nf_conn_rtcache *rtc;

	rtc = nf_ct_ext_add(ct, NF_CT_EXT_RTCACHE, GFP_ATOMIC);
	if (rtc) {
		rtc->cached_dst[IP_CT_DIR_ORIGINAL].iif = -1;
		rtc->cached_dst[IP_CT_DIR_ORIGINAL].dst = NULL;
		rtc->cached_dst[IP_CT_DIR_REPLY].iif = -1;
		rtc->cached_dst[IP_CT_DIR_REPLY].dst = NULL;
	}
}

static struct nf_conn_rtcache *nf_ct_rtcache_find_usable(struct nf_conn *ct)
{
	return nf_ct_rtcache_find(ct);
}

static struct dst_entry *
nf_conn_rtcache_dst_get(const struct nf_conn_rtcache *rtc,
			enum ip_conntrack_dir dir)
{
	return rtc->cached_dst[dir].dst;
}

static u32 nf_rtcache_get_cookie(int pf, const struct dst_entry *dst)
{
#if IS_ENABLED(CONFIG_NF_CONNTRACK_IPV6)
	if (pf == NFPROTO_IPV6) {
		const struct rt6_info *rt = (const struct rt6_info *)dst;

		if (rt->from && rt->from->fib6_node)
			return (u32)rt->from->fib6_node->fn_sernum;
	}
#endif
	return 0;
}

static void nf_conn_rtcache_dst_set(int pf,
				    struct nf_conn_rtcache *rtc,
				    struct dst_entry *dst,
				    enum ip_conntrack_dir dir, int iif)
{
	if (rtc->cached_dst[dir].iif != iif)
		rtc->cached_dst[dir].iif = iif;

	if (rtc->cached_dst[dir].dst != dst) {
		struct dst_entry *old;

		dst_hold(dst);

		old = xchg(&rtc->cached_dst[dir].dst, dst);
		dst_release(old);

#if IS_ENABLED(CONFIG_NF_CONNTRACK_IPV6)
		if (pf == NFPROTO_IPV6)
			rtc->cached_dst[dir].cookie =
				nf_rtcache_get_cookie(pf, dst);
#endif
	}
}

static void nf_conn_rtcache_dst_obsolete(struct nf_conn_rtcache *rtc,
					 enum ip_conntrack_dir dir)
{
	pr_debug("Invalidate iif %d for dir %d on cache %p\n",
		 rtc->cached_dst[dir].iif, dir, rtc);

	rtc->cached_dst[dir].iif = -1;
}

static unsigned int nf_rtcache_in(u_int8_t pf,
				  struct sk_buff *skb,
				  const struct nf_hook_state *state)
{
	struct nf_conn_rtcache *rtc;
	enum ip_conntrack_info ctinfo;
	enum ip_conntrack_dir dir;
	struct dst_entry *dst;
	struct nf_conn *ct;
	int iif;
	u32 cookie;

	if (skb_dst(skb) || skb->sk)
		return NF_ACCEPT;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct)
		return NF_ACCEPT;

	rtc = nf_ct_rtcache_find_usable(ct);
	if (!rtc)
		return NF_ACCEPT;

	/* if iif changes, don't use cache and let ip stack
	 * do route lookup.
	 *
	 * If rp_filter is enabled it might toss skb, so
	 * we don't want to avoid these checks.
	 */
	dir = CTINFO2DIR(ctinfo);
	iif = nf_conn_rtcache_iif_get(rtc, dir);
	if (state->in->ifindex != iif) {
		pr_debug("ct %p, iif %d, cached iif %d, skip cached entry\n",
			 ct, iif, state->in->ifindex);
		return NF_ACCEPT;
	}
	dst = nf_conn_rtcache_dst_get(rtc, dir);
	if (dst == NULL)
		return NF_ACCEPT;

	cookie = nf_rtcache_get_cookie(pf, dst);

	dst = dst_check(dst, cookie);
	pr_debug("obtained dst %p for skb %p, cookie %d\n", dst, skb, cookie);
	if (likely(dst))
		skb_dst_set_noref(skb, dst);
	else
		nf_conn_rtcache_dst_obsolete(rtc, dir);

	return NF_ACCEPT;
}

static unsigned int nf_rtcache_forward(u_int8_t pf,
				       struct sk_buff *skb,
				       const struct nf_hook_state *state)
{
	struct nf_conn_rtcache *rtc;
	enum ip_conntrack_info ctinfo;
	enum ip_conntrack_dir dir;
	struct nf_conn *ct;
	struct dst_entry *dst = skb_dst(skb);
	int iif;

	ct = nf_ct_get(skb, &ctinfo);
	if (!ct)
		return NF_ACCEPT;

	if (dst && dst_xfrm(dst))
		return NF_ACCEPT;

	if (dst && (dst->flags & DST_FAKE_RTABLE))
		return NF_ACCEPT;

	if (!nf_ct_is_confirmed(ct)) {
		if (nf_ct_rtcache_find(ct))
			return NF_ACCEPT;
		nf_ct_rtcache_ext_add(ct);
		return NF_ACCEPT;
	}

	rtc = nf_ct_rtcache_find_usable(ct);
	if (!rtc)
		return NF_ACCEPT;

	dir = CTINFO2DIR(ctinfo);
	iif = nf_conn_rtcache_iif_get(rtc, dir);
	pr_debug("ct %p, skb %p, dir %d, iif %d, cached iif %d\n",
		 ct, skb, dir, iif, state->in->ifindex);
	if (likely(state->in->ifindex == iif))
		return NF_ACCEPT;

	/*
	 * If the dst entry's refcnt is 0, then skip adding this to
	 * the rtcache as its free is already scheduled.
	 */
	dst = skb_dst(skb);
	if (unlikely(atomic_read(&dst->__refcnt) == 0)) {
		return NF_ACCEPT;
	}

	nf_conn_rtcache_dst_set(pf, rtc, dst, dir, state->in->ifindex);
	return NF_ACCEPT;
}

static unsigned int nf_rtcache_in4(void *priv,
				  struct sk_buff *skb,
				  const struct nf_hook_state *state)
{
	return nf_rtcache_in(NFPROTO_IPV4, skb, state);
}

static unsigned int nf_rtcache_forward4(void *priv,
				       struct sk_buff *skb,
				       const struct nf_hook_state *state)
{
	return nf_rtcache_forward(NFPROTO_IPV4, skb, state);
}

#if IS_ENABLED(CONFIG_NF_CONNTRACK_IPV6)
static unsigned int nf_rtcache_in6(void *priv,
				  struct sk_buff *skb,
				  const struct nf_hook_state *state)
{
	return nf_rtcache_in(NFPROTO_IPV6, skb, state);
}

static unsigned int nf_rtcache_forward6(void *priv,
				       struct sk_buff *skb,
				       const struct nf_hook_state *state)
{
 	return nf_rtcache_forward(NFPROTO_IPV6, skb, state);
}
#endif

static int nf_rtcache_dst_remove(struct nf_conn *ct, void *data)
{
	struct nf_conn_rtcache *rtc = nf_ct_rtcache_find(ct);
	struct net_device *dev = data;

	if (!rtc)
		return 0;

	if (dev->ifindex == rtc->cached_dst[IP_CT_DIR_ORIGINAL].iif ||
	    dev->ifindex == rtc->cached_dst[IP_CT_DIR_REPLY].iif) {
		nf_conn_rtcache_destroy(ct);
	}

	return 0;
}

static int nf_rtcache_netdev_event(struct notifier_block *this,
				   unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct net *net = dev_net(dev);

	if (event == NETDEV_DOWN)
		nf_ct_iterate_cleanup_net(net, nf_rtcache_dst_remove, dev, 0, 0);

	return NOTIFY_DONE;
}

static struct notifier_block nf_rtcache_notifier = {
	.notifier_call = nf_rtcache_netdev_event,
};

static struct nf_hook_ops rtcache_ops[] = {
	{
		.hook		= nf_rtcache_in4,
		.pf		= NFPROTO_IPV4,
		.hooknum	= NF_INET_PRE_ROUTING,
		.priority       = NF_IP_PRI_LAST,
	},
	{
		.hook           = nf_rtcache_forward4,
		.pf             = NFPROTO_IPV4,
		.hooknum        = NF_INET_FORWARD,
		.priority       = NF_IP_PRI_LAST,
	},
#if IS_ENABLED(CONFIG_NF_CONNTRACK_IPV6)
	{
		.hook		= nf_rtcache_in6,
		.pf		= NFPROTO_IPV6,
		.hooknum	= NF_INET_PRE_ROUTING,
		.priority       = NF_IP_PRI_LAST,
	},
	{
		.hook           = nf_rtcache_forward6,
		.pf             = NFPROTO_IPV6,
		.hooknum        = NF_INET_FORWARD,
		.priority       = NF_IP_PRI_LAST,
	},
#endif
};

static struct nf_ct_ext_type rtcache_extend __read_mostly = {
	.len	= sizeof(struct nf_conn_rtcache),
	.align	= __alignof__(struct nf_conn_rtcache),
	.id	= NF_CT_EXT_RTCACHE,
	.destroy = nf_conn_rtcache_destroy,
};

static int __net_init rtcache_net_init(struct net *net)
{
	return nf_register_net_hooks(net, rtcache_ops, ARRAY_SIZE(rtcache_ops));
}

static void __net_exit rtcache_net_exit(struct net *net)
{
	/* remove hooks so no new connections get rtcache extension */
	nf_unregister_net_hooks(net, rtcache_ops, ARRAY_SIZE(rtcache_ops));
}

static struct pernet_operations rtcache_ops_net_ops = {
	.init	= rtcache_net_init,
	.exit	= rtcache_net_exit,
};

static int __init nf_conntrack_rtcache_init(void)
{
	int ret = nf_ct_extend_register(&rtcache_extend);

	if (ret < 0) {
		pr_err("nf_conntrack_rtcache: Unable to register extension\n");
		return ret;
	}

	ret = register_pernet_subsys(&rtcache_ops_net_ops);
	if (ret) {
		nf_ct_extend_unregister(&rtcache_extend);
		return ret;
	}

	ret = register_netdevice_notifier(&nf_rtcache_notifier);
	if (ret) {
		nf_ct_extend_unregister(&rtcache_extend);
		unregister_pernet_subsys(&rtcache_ops_net_ops);
	}

	return ret;
}

static int nf_rtcache_ext_remove(struct nf_conn *ct, void *data)
{
	struct nf_conn_rtcache *rtc = nf_ct_rtcache_find(ct);

	return rtc != NULL;
}

static bool __exit nf_conntrack_rtcache_wait_for_dying(struct net *net)
{
	bool wait = false;
	int cpu;

	for_each_possible_cpu(cpu) {
		struct nf_conntrack_tuple_hash *h;
		struct hlist_nulls_node *n;
		struct nf_conn *ct;
		struct ct_pcpu *pcpu = per_cpu_ptr(net->ct.pcpu_lists, cpu);

		rcu_read_lock();
		spin_lock_bh(&pcpu->lock);

		hlist_nulls_for_each_entry(h, n, &pcpu->dying, hnnode) {
			ct = nf_ct_tuplehash_to_ctrack(h);
			if (nf_ct_rtcache_find(ct) != NULL) {
				wait = true;
				break;
			}
		}
		spin_unlock_bh(&pcpu->lock);
		rcu_read_unlock();
	}

	return wait;
}

static void __exit nf_conntrack_rtcache_fini(void)
{
	struct net *net;
	int count = 0;

	synchronize_net();

	unregister_netdevice_notifier(&nf_rtcache_notifier);
	unregister_pernet_subsys(&rtcache_ops_net_ops);

	synchronize_net();

	rtnl_lock();

	/* zap all conntracks with rtcache extension */
	for_each_net(net)
		nf_ct_iterate_cleanup_net(net, nf_rtcache_ext_remove, NULL, 0, 0);

	for_each_net(net) {
		/* .. and make sure they're gone from dying list, too */
		while (nf_conntrack_rtcache_wait_for_dying(net)) {
			msleep(200);
			WARN_ONCE(++count > 25, "Waiting for all rtcache conntracks to go away\n");
		}
	}

	rtnl_unlock();

	synchronize_net();
	nf_ct_extend_unregister(&rtcache_extend);
}
module_init(nf_conntrack_rtcache_init);
module_exit(nf_conntrack_rtcache_fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Florian Westphal <fw@strlen.de>");
MODULE_DESCRIPTION("Conntrack route cache extension");
