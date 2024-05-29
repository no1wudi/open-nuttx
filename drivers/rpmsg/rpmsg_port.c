/****************************************************************************
 * drivers/rpmsg/rpmsg_port.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdio.h>

#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>

#include <metal/mutex.h>

#include <rpmsg/rpmsg_internal.h>

#include "rpmsg_port.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define RPMSG_PORT_BUF_TO_NODE(q,b) ((q)->node + ((FAR void *)(b) - (q)->buf) / (q)->len)
#define RPMSG_PORT_NODE_TO_BUF(q,n) ((q)->buf + (((n) - (q)->node)) * (q)->len)

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static FAR const char *rpmsg_port_get_cpuname(FAR struct rpmsg_s *rpmsg);
static int rpmsg_port_get_tx_buffer_size(FAR struct rpmsg_s *rpmsg);
static int rpmsg_port_get_rx_buffer_size(FAR struct rpmsg_s *rpmsg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct rpmsg_ops_s g_rpmsg_port_ops =
{
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  rpmsg_port_get_cpuname,
  rpmsg_port_get_tx_buffer_size,
  rpmsg_port_get_rx_buffer_size,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rpmsg_port_post
 ****************************************************************************/

static void rpmsg_port_post(FAR sem_t *sem)
{
  int count = 0;

  nxsem_get_value(sem, &count);
  while (count++ <= 0)
    {
      nxsem_post(sem);
    }
}

/****************************************************************************
 * Name: rpmsg_port_add_node
 ****************************************************************************/

static void rpmsg_port_add_node(FAR struct rpmsg_port_list_s *list,
                                FAR struct list_node *node)
{
  irqstate_t flags;

  flags = spin_lock_irqsave(&list->lock);
  list_add_tail(&list->head, node);
  list->num++;
  spin_unlock_irqrestore(&list->lock, flags);
}

/****************************************************************************
 * Name: rpmsg_port_remove_node
 ****************************************************************************/

static FAR struct list_node *
rpmsg_port_remove_node(FAR struct rpmsg_port_list_s *list)
{
  FAR struct list_node *node;
  irqstate_t flags;

  flags = spin_lock_irqsave(&list->lock);
  node = list_remove_head(&list->head);
  if (node != NULL)
    {
      list->num--;
    }

  spin_unlock_irqrestore(&list->lock, flags);
  return node;
}

/****************************************************************************
 * Name: rpmsg_port_destroy_queue
 *
 * Description:
 *   Free memory alloced by rpmsg_port_create_queue.
 *
 ****************************************************************************/

static void rpmsg_port_destroy_queue(FAR struct rpmsg_port_queue_s *queue)
{
  if (queue->alloced)
    {
      kmm_free(queue->buf);
    }

  kmm_free(queue->node);
  nxsem_destroy(&queue->free.sem);
  nxsem_destroy(&queue->ready.sem);
}

/****************************************************************************
 * Name: rpmsg_port_create_queue
 ****************************************************************************/

static int rpmsg_port_create_queue(FAR struct rpmsg_port_queue_s *queue,
                                   uint32_t count, uint32_t len,
                                   FAR void *buf)
{
  FAR struct list_node *node;

  node = kmm_malloc(count * sizeof(struct list_node));
  if (node == NULL)
    {
      return -ENOMEM;
    }

  queue->node = node;

  /* Check if buffer space needed to be malloced internal. */

  if (buf == NULL)
    {
      buf = kmm_malloc(count * len);
      if (buf == NULL)
        {
          kmm_free(queue->node);
          return -ENOMEM;
        }

      queue->alloced = true;
    }

  queue->len = len;
  queue->buf = buf;

  /* Init free list */

  spin_lock_init(&queue->free.lock);
  nxsem_init(&queue->free.sem, 0, 0);
  list_initialize(&queue->free.head);
  while (count--)
    {
      rpmsg_port_add_node(&queue->free, node);
      node++;
    }

  /* Init ready list */

  spin_lock_init(&queue->ready.lock);
  nxsem_init(&queue->ready.sem, 0, 0);
  list_initialize(&queue->ready.head);

  return 0;
}

/****************************************************************************
 * Name: rpmsg_port_create_queues
 ****************************************************************************/

static int
rpmsg_port_create_queues(FAR struct rpmsg_port_s *port,
                         FAR const struct rpmsg_port_config_s *cfg)
{
  int ret;

  ret = rpmsg_port_create_queue(&port->txq, cfg->txnum,
                                cfg->txlen, cfg->txbuf);
  if (ret < 0)
    {
      return ret;
    }

  ret = rpmsg_port_create_queue(&port->rxq, cfg->rxnum,
                                cfg->rxlen, cfg->rxbuf);
  if (ret < 0)
    {
      rpmsg_port_destroy_queue(&port->txq);
      return ret;
    }

  return 0;
}

/****************************************************************************
 * Name: rpmsg_port_destroy_queues
 ****************************************************************************/

static void rpmsg_port_destroy_queues(FAR struct rpmsg_port_s *port)
{
  rpmsg_port_destroy_queue(&port->txq);
  rpmsg_port_destroy_queue(&port->rxq);
}

/****************************************************************************
 * Name: rpmsg_port_get_tx_payload_buffer
 ****************************************************************************/

static FAR void *
rpmsg_port_get_tx_payload_buffer(FAR struct rpmsg_device *rdev,
                                 FAR uint32_t *len, int wait)
{
  FAR struct rpmsg_port_s *port =
    metal_container_of(rdev, struct rpmsg_port_s, rdev);
  FAR struct rpmsg_port_header_s *hdr =
    rpmsg_port_queue_get_available_buffer(&port->txq, wait);

  if (hdr == NULL)
    {
      return NULL;
    }

  *len = hdr->len - sizeof(struct rpmsg_port_header_s) -
         sizeof(struct rpmsg_hdr);

  return RPMSG_LOCATE_DATA(hdr->buf);
}

/****************************************************************************
 * Name: rpmsg_port_send_offchannel_nocopy
 ****************************************************************************/

static int rpmsg_port_send_offchannel_nocopy(FAR struct rpmsg_device *rdev,
                                             uint32_t src, uint32_t dst,
                                             FAR const void *data, int len)
{
  FAR struct rpmsg_port_s *port =
    metal_container_of(rdev, struct rpmsg_port_s, rdev);
  FAR struct rpmsg_port_header_s *hdr;
  FAR struct rpmsg_hdr *rp_hdr;

  rp_hdr = RPMSG_LOCATE_HDR(data);
  rp_hdr->dst = dst;
  rp_hdr->src = src;
  rp_hdr->len = len;
  rp_hdr->reserved = 0;
  rp_hdr->flags = 0;

  hdr = metal_container_of(rp_hdr, struct rpmsg_port_header_s, buf);
  hdr->len = sizeof(struct rpmsg_port_header_s) +
             sizeof(struct rpmsg_hdr) + len;

  rpmsg_port_queue_add_buffer(&port->txq, hdr);
  port->ops->notify_tx_ready(port);

  return len;
}

/****************************************************************************
 * Name: rpmsg_port_send_offchannel_raw
 ****************************************************************************/

static int rpmsg_port_send_offchannel_raw(FAR struct rpmsg_device *rdev,
                                          uint32_t src, uint32_t dst,
                                          FAR const void *data,
                                          int len, int wait)
{
  uint32_t buflen;
  FAR void *buf;

  buf = rpmsg_port_get_tx_payload_buffer(rdev, &buflen, wait);
  if (buf == NULL)
    {
      return RPMSG_ERR_NO_BUFF;
    }

  RPMSG_ASSERT(len <= buflen, "Send size larger than buffer size\n");
  memcpy(buf, data, len);

  return rpmsg_port_send_offchannel_nocopy(rdev, src, dst, buf, len);
}

/****************************************************************************
 * Name: rpmsg_port_hold_rx_buffer
 ****************************************************************************/

static void rpmsg_port_hold_rx_buffer(FAR struct rpmsg_device *rdev,
                                      FAR void *rxbuf)
{
  FAR struct rpmsg_hdr *rp_hdr = RPMSG_LOCATE_HDR(rxbuf);

  atomic_fetch_add(&rp_hdr->reserved, 1 << RPMSG_BUF_HELD_SHIFT);
}

/****************************************************************************
 * Name: rpmsg_port_release_rx_buffer
 ****************************************************************************/

static void rpmsg_port_release_rx_buffer(FAR struct rpmsg_device *rdev,
                                         FAR void *rxbuf)
{
  FAR struct rpmsg_port_s *port =
    metal_container_of(rdev, struct rpmsg_port_s, rdev);
  FAR struct rpmsg_hdr *rp_hdr = RPMSG_LOCATE_HDR(rxbuf);
  FAR struct rpmsg_port_header_s *hdr =
    metal_container_of(rp_hdr, struct rpmsg_port_header_s, buf);
  uint32_t reserved =
    atomic_fetch_sub(&rp_hdr->reserved, 1 << RPMSG_BUF_HELD_SHIFT);

  if ((reserved & RPMSG_BUF_HELD_MASK) == (1 << RPMSG_BUF_HELD_SHIFT))
    {
      rpmsg_port_queue_return_buffer(&port->rxq, hdr);
      port->ops->notify_rx_free(port);
    }
}

/****************************************************************************
 * Name: rpmsg_port_release_tx_buffer
 ****************************************************************************/

static int rpmsg_port_release_tx_buffer(FAR struct rpmsg_device *rdev,
                                        FAR void *txbuf)
{
  FAR struct rpmsg_port_s *port =
    metal_container_of(rdev, struct rpmsg_port_s, rdev);
  FAR struct rpmsg_hdr *rp_hdr = RPMSG_LOCATE_HDR(txbuf);
  FAR struct rpmsg_port_header_s *hdr =
    metal_container_of(rp_hdr, struct rpmsg_port_header_s, buf);

  rpmsg_port_queue_return_buffer(&port->txq, hdr);
  return RPMSG_SUCCESS;
}

/****************************************************************************
 * Name: rpmsg_port_rx_callback
 ****************************************************************************/

static void rpmsg_port_rx_callback(FAR struct rpmsg_port_s *port,
                                   FAR struct rpmsg_port_header_s *hdr)
{
  FAR struct rpmsg_device *rdev = &port->rdev;
  FAR struct rpmsg_hdr *rp_hdr = (FAR struct rpmsg_hdr *)hdr->buf;
  FAR void *data = RPMSG_LOCATE_DATA(rp_hdr);
  FAR struct rpmsg_endpoint *ept;
  int status;

  metal_mutex_acquire(&rdev->lock);
  ept = rpmsg_get_ept_from_addr(rdev, rp_hdr->dst);
  rpmsg_ept_incref(ept);
  metal_mutex_release(&rdev->lock);
  rpmsg_port_hold_rx_buffer(rdev, data);

  if (ept != NULL)
    {
      if (ept->dest_addr == RPMSG_ADDR_ANY)
        {
          ept->dest_addr = rp_hdr->src;
        }

      status = ept->cb(ept, data, rp_hdr->len, rp_hdr->src, ept->priv);
      if (status < 0)
        {
          RPMSG_ASSERT(0, "unexpected callback status\n");
        }
    }

  rpmsg_port_release_rx_buffer(rdev, data);
  metal_mutex_acquire(&rdev->lock);
  rpmsg_ept_decref(ept);
  metal_mutex_release(&rdev->lock);
}

/****************************************************************************
 * Name: rpmsg_port_ns_callback
 ****************************************************************************/

static int rpmsg_port_ns_callback(FAR struct rpmsg_endpoint *ept,
                                  FAR void *data, size_t len, uint32_t src,
                                  FAR void *priv)
{
  FAR struct rpmsg_device *rdev = ept->rdev;
  FAR struct rpmsg_ns_msg *msg = data;
  FAR const char *name = msg->name;
  uint32_t dest = msg->addr;
  bool decref = false;

  if (len != sizeof(*msg))
    {
      return RPMSG_SUCCESS;
    }

  metal_mutex_acquire(&rdev->lock);
  ept = rpmsg_get_endpoint(rdev, name, RPMSG_ADDR_ANY, dest);

  if (msg->flags == RPMSG_NS_DESTROY)
    {
      if (ept != NULL)
        {
          ept->dest_addr = RPMSG_ADDR_ANY;
          if (ept->release_cb != NULL)
            {
              rpmsg_ept_incref(ept);
              decref = true;
            }
        }

      metal_mutex_release(&rdev->lock);
      if (ept != NULL && ept->ns_unbind_cb != NULL)
        {
          ept->ns_unbind_cb(ept);
        }

      if (rdev->ns_unbind_cb != NULL)
        {
          rdev->ns_unbind_cb(rdev, name, dest);
        }

      if (decref)
        {
          metal_mutex_acquire(&rdev->lock);
          rpmsg_ept_decref(ept);
          metal_mutex_release(&rdev->lock);
        }
    }
  else if (msg->flags == RPMSG_NS_CREATE)
    {
      if (ept == NULL)
        {
          metal_mutex_release(&rdev->lock);
          if (rdev->ns_bind_cb != NULL)
            {
              rdev->ns_bind_cb(rdev, name, dest);
            }
        }
      else if (ept->dest_addr == RPMSG_ADDR_ANY)
        {
          ept->dest_addr = dest;
          metal_mutex_release(&rdev->lock);
          if (ept->name[0] && rdev->support_ack)
            {
              rpmsg_send_ns_message(ept, RPMSG_NS_CREATE_ACK);
            }

          /* Notify application that the endpoint has been bound */

          if (ept->ns_bound_cb != NULL)
            {
              ept->ns_bound_cb(ept);
            }
        }
      else
        {
          metal_mutex_release(&rdev->lock);
        }
    }
  else
    {
      /* RPMSG_NS_CREATE_ACK */

      if (ept != NULL && ept->dest_addr == RPMSG_ADDR_ANY)
        {
          ept->dest_addr = dest;
          metal_mutex_release(&rdev->lock);

          if (ept->ns_bound_cb != NULL)
            {
              ept->ns_bound_cb(ept);
            }
        }
      else
        {
          metal_mutex_release(&rdev->lock);
        }
    }

  return RPMSG_SUCCESS;
}

/****************************************************************************
 * Name: rpmsg_port_get_cpuname
 ****************************************************************************/

static FAR const char *rpmsg_port_get_cpuname(FAR struct rpmsg_s *rpmsg)
{
  FAR struct rpmsg_port_s *port = (FAR struct rpmsg_port_s *)rpmsg;

  return port->cpuname;
}

/****************************************************************************
 * Name: rpmsg_port_get_tx_buffer_size
 ****************************************************************************/

static int rpmsg_port_get_tx_buffer_size(FAR struct rpmsg_s *rpmsg)
{
  FAR struct rpmsg_port_s *port = (FAR struct rpmsg_port_s *)rpmsg;

  return port->txq.len - sizeof(struct rpmsg_port_header_s) -
         sizeof(struct rpmsg_hdr);
}

/****************************************************************************
 * Name: rpmsg_port_get_rx_buffer_size
 ****************************************************************************/

static int rpmsg_port_get_rx_buffer_size(FAR struct rpmsg_s *rpmsg)
{
  FAR struct rpmsg_port_s *port = (FAR struct rpmsg_port_s *)rpmsg;

  return port->rxq.len - sizeof(struct rpmsg_port_header_s) -
         sizeof(struct rpmsg_hdr);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rpmsg_port_initialize
 ****************************************************************************/

int rpmsg_port_initialize(FAR struct rpmsg_port_s *port,
                          FAR const struct rpmsg_port_config_s *cfg,
                          FAR const struct rpmsg_port_ops_s *ops)
{
  FAR struct rpmsg_device *rdev;
  int ret;

  if (port == NULL || cfg == NULL || ops == NULL)
    {
      return -EINVAL;
    }

  ret = rpmsg_port_create_queues(port, cfg);
  if (ret < 0)
    {
      return ret;
    }

  port->ops = ops;
  strlcpy(port->cpuname, cfg->remotecpu, RPMSG_NAME_SIZE);

  rdev = &port->rdev;
  memset(rdev, 0, sizeof(*rdev));
  metal_mutex_init(&rdev->lock);
  rdev->ns_bind_cb = rpmsg_ns_bind;
  rdev->ns_unbind_cb = rpmsg_ns_unbind;
  rdev->ops.send_offchannel_raw = rpmsg_port_send_offchannel_raw;
  rdev->ops.hold_rx_buffer = rpmsg_port_hold_rx_buffer;
  rdev->ops.release_rx_buffer = rpmsg_port_release_rx_buffer;
  rdev->ops.get_tx_payload_buffer = rpmsg_port_get_tx_payload_buffer;
  rdev->ops.send_offchannel_nocopy = rpmsg_port_send_offchannel_nocopy;
  rdev->ops.release_tx_buffer = rpmsg_port_release_tx_buffer;

  metal_list_init(&rdev->endpoints);

  rdev->support_ack = true;
  rdev->support_ns = true;

  rpmsg_register_endpoint(rdev, &rdev->ns_ept, "NS", RPMSG_NS_EPT_ADDR,
                          RPMSG_NS_EPT_ADDR, rpmsg_port_ns_callback, NULL);
  port->ops->register_callback(port, rpmsg_port_rx_callback);

  return 0;
}

/****************************************************************************
 * Name: rpmsg_port_uninitialize
 ****************************************************************************/

void rpmsg_port_uninitialize(FAR struct rpmsg_port_s *port)
{
  FAR struct rpmsg_device *rdev = &port->rdev;
  FAR struct metal_list *node;
  FAR struct rpmsg_endpoint *ept;

  while (!metal_list_is_empty(&rdev->endpoints))
    {
      node = rdev->endpoints.next;
      ept = metal_container_of(node, struct rpmsg_endpoint, node);
      rpmsg_destroy_ept(ept);
      if (ept->ns_unbind_cb)
        {
          ept->ns_unbind_cb(ept);
        }
    }

  metal_mutex_deinit(&rdev->lock);
  rpmsg_port_destroy_queues(port);
}

/****************************************************************************
 * Name: rpmsg_port_queue_get_available_buffer
 ****************************************************************************/

FAR struct rpmsg_port_header_s *
rpmsg_port_queue_get_available_buffer(FAR struct rpmsg_port_queue_s *queue,
                                      bool wait)
{
  FAR struct list_node *node;
  FAR struct rpmsg_port_header_s *hdr;

  for (; ; )
    {
      node = rpmsg_port_remove_node(&queue->free);
      if (node)
        {
          hdr = RPMSG_PORT_NODE_TO_BUF(queue, node);
          hdr->len = queue->len;
          return hdr;
        }
      else if (!wait)
        {
          return NULL;
        }

      nxsem_wait_uninterruptible(&queue->free.sem);
    }
}

/****************************************************************************
 * Name: rpmsg_port_queue_return_buffer
 ****************************************************************************/

FAR void rpmsg_port_queue_return_buffer(FAR struct rpmsg_port_queue_s *queue,
                                        FAR struct rpmsg_port_header_s *hdr)
{
  FAR struct list_node *node = RPMSG_PORT_BUF_TO_NODE(queue, hdr);

  rpmsg_port_add_node(&queue->free, node);
  rpmsg_port_post(&queue->free.sem);
}

/****************************************************************************
 * Name: rpmsg_port_queue_get_buffer
 ****************************************************************************/

FAR struct rpmsg_port_header_s *
rpmsg_port_queue_get_buffer(FAR struct rpmsg_port_queue_s *queue, bool wait)
{
  FAR struct list_node *node;

  for (; ; )
    {
      node = rpmsg_port_remove_node(&queue->ready);
      if (node)
        {
          return RPMSG_PORT_NODE_TO_BUF(queue, node);
        }
      else if (!wait)
        {
          return NULL;
        }

      nxsem_wait_uninterruptible(&queue->ready.sem);
    }
}

/****************************************************************************
 * Name: rpmsg_port_queue_add_buffer
 ****************************************************************************/

void rpmsg_port_queue_add_buffer(FAR struct rpmsg_port_queue_s *queue,
                                 FAR struct rpmsg_port_header_s *hdr)
{
  FAR struct list_node *node = RPMSG_PORT_BUF_TO_NODE(queue, hdr);

  rpmsg_port_add_node(&queue->ready, node);
  rpmsg_port_post(&queue->ready.sem);
}

/****************************************************************************
 * Name: rpmsg_port_register
 ****************************************************************************/

int rpmsg_port_register(FAR struct rpmsg_port_s *port)
{
  char name[64];
  int ret;

  snprintf(name, sizeof(name), "/dev/rpmsg/%s", port->cpuname);
  ret = rpmsg_register(name, &port->rpmsg, &g_rpmsg_port_ops);
  if (ret < 0)
    {
      return ret;
    }

  rpmsg_device_created(&port->rpmsg);
  return ret;
}

/****************************************************************************
 * Name: rpmsg_port_unregister
 ****************************************************************************/

void rpmsg_port_unregister(FAR struct rpmsg_port_s *port)
{
  char name[64];

  rpmsg_device_destory(&port->rpmsg);

  snprintf(name, sizeof(name), "/dev/rpmsg/%s", port->cpuname);
  rpmsg_unregister(name, &port->rpmsg);
}