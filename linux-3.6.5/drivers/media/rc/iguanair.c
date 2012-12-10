/*
 * IguanaWorks USB IR Transceiver support
 *
 * Copyright (C) 2012 Sean Young <sean@mess.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb/input.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <media/rc-core.h>

#define DRIVER_NAME "iguanair"

struct iguanair {
	struct rc_dev *rc;

	struct device *dev;
	struct usb_device *udev;

	int pipe_in, pipe_out;
	uint8_t bufsize;
	uint8_t version[2];

	struct mutex lock;

	/* receiver support */
	bool receiver_on;
	dma_addr_t dma_in;
	uint8_t *buf_in;
	struct urb *urb_in;
	struct completion completion;

	/* transmit support */
	bool tx_overflow;
	uint32_t carrier;
	uint8_t cycle_overhead;
	uint8_t channels;
	uint8_t busy4;
	uint8_t busy7;

	char name[64];
	char phys[64];
};

#define CMD_GET_VERSION		0x01
#define CMD_GET_BUFSIZE		0x11
#define CMD_GET_FEATURES	0x10
#define CMD_SEND		0x15
#define CMD_EXECUTE		0x1f
#define CMD_RX_OVERFLOW		0x31
#define CMD_TX_OVERFLOW		0x32
#define CMD_RECEIVER_ON		0x12
#define CMD_RECEIVER_OFF	0x14

#define DIR_IN			0xdc
#define DIR_OUT			0xcd

#define MAX_PACKET_SIZE		8u
#define TIMEOUT			1000

struct packet {
	uint16_t start;
	uint8_t direction;
	uint8_t cmd;
};

struct response_packet {
	struct packet header;
	uint8_t data[4];
};

struct send_packet {
	struct packet header;
	uint8_t length;
	uint8_t channels;
	uint8_t busy7;
	uint8_t busy4;
	uint8_t payload[0];
};

static void process_ir_data(struct iguanair *ir, unsigned len)
{
	if (len >= 4 && ir->buf_in[0] == 0 && ir->buf_in[1] == 0) {
		switch (ir->buf_in[3]) {
		case CMD_TX_OVERFLOW:
			ir->tx_overflow = true;
		case CMD_RECEIVER_OFF:
		case CMD_RECEIVER_ON:
		case CMD_SEND:
			complete(&ir->completion);
			break;
		case CMD_RX_OVERFLOW:
			dev_warn(ir->dev, "receive overflow\n");
			break;
		default:
			dev_warn(ir->dev, "control code %02x received\n",
							ir->buf_in[3]);
			break;
		}
	} else if (len >= 7) {
		DEFINE_IR_RAW_EVENT(rawir);
		unsigned i;

		init_ir_raw_event(&rawir);

		for (i = 0; i < 7; i++) {
			if (ir->buf_in[i] == 0x80) {
				rawir.pulse = false;
				rawir.duration = US_TO_NS(21845);
			} else {
				rawir.pulse = (ir->buf_in[i] & 0x80) == 0;
				rawir.duration = ((ir->buf_in[i] & 0x7f) + 1) *
									 21330;
			}

			ir_raw_event_store_with_filter(ir->rc, &rawir);
		}

		ir_raw_event_handle(ir->rc);
	}
}

static void iguanair_rx(struct urb *urb)
{
	struct iguanair *ir;

	if (!urb)
		return;

	ir = urb->context;
	if (!ir) {
		usb_unlink_urb(urb);
		return;
	}

	switch (urb->status) {
	case 0:
		process_ir_data(ir, urb->actual_length);
		break;
	case -ECONNRESET:
	case -ENOENT:
	case -ESHUTDOWN:
		usb_unlink_urb(urb);
		return;
	case -EPIPE:
	default:
		dev_dbg(ir->dev, "Error: urb status = %d\n", urb->status);
		break;
	}

	usb_submit_urb(urb, GFP_ATOMIC);
}

static int iguanair_send(struct iguanair *ir, void *data, unsigned size,
			struct response_packet *response, unsigned *res_len)
{
	unsigned offset, len;
	int rc, transferred;

	for (offset = 0; offset < size; offset += MAX_PACKET_SIZE) {
		len = min(size - offset, MAX_PACKET_SIZE);

		if (ir->tx_overflow)
			return -EOVERFLOW;

		rc = usb_interrupt_msg(ir->udev, ir->pipe_out, data + offset,
						len, &transferred, TIMEOUT);
		if (rc)
			return rc;

		if (transferred != len)
			return -EIO;
	}

	if (response) {
		rc = usb_interrupt_msg(ir->udev, ir->pipe_in, response,
					sizeof(*response), res_len, TIMEOUT);
	}

	return rc;
}

static int iguanair_get_features(struct iguanair *ir)
{
	struct packet packet;
	struct response_packet response;
	int rc, len;

	packet.start = 0;
	packet.direction = DIR_OUT;
	packet.cmd = CMD_GET_VERSION;

	rc = iguanair_send(ir, &packet, sizeof(packet), &response, &len);
	if (rc) {
		dev_info(ir->dev, "failed to get version\n");
		goto out;
	}

	if (len != 6) {
		dev_info(ir->dev, "failed to get version\n");
		rc = -EIO;
		goto out;
	}

	ir->version[0] = response.data[0];
	ir->version[1] = response.data[1];
	ir->bufsize = 150;
	ir->cycle_overhead = 65;

	packet.cmd = CMD_GET_BUFSIZE;

	rc = iguanair_send(ir, &packet, sizeof(packet), &response, &len);
	if (rc) {
		dev_info(ir->dev, "failed to get buffer size\n");
		goto out;
	}

	if (len != 5) {
		dev_info(ir->dev, "failed to get buffer size\n");
		rc = -EIO;
		goto out;
	}

	ir->bufsize = response.data[0];

	if (ir->version[0] == 0 || ir->version[1] == 0)
		goto out;

	packet.cmd = CMD_GET_FEATURES;

	rc = iguanair_send(ir, &packet, sizeof(packet), &response, &len);
	if (rc) {
		dev_info(ir->dev, "failed to get features\n");
		goto out;
	}

	if (len < 5) {
		dev_info(ir->dev, "failed to get features\n");
		rc = -EIO;
		goto out;
	}

	if (len > 5 && ir->version[0] >= 4)
		ir->cycle_overhead = response.data[1];

out:
	return rc;
}

static int iguanair_receiver(struct iguanair *ir, bool enable)
{
	struct packet packet = { 0, DIR_OUT, enable ?
				CMD_RECEIVER_ON : CMD_RECEIVER_OFF };
	int rc;

	INIT_COMPLETION(ir->completion);

	rc = iguanair_send(ir, &packet, sizeof(packet), NULL, NULL);
	if (rc)
		return rc;

	wait_for_completion_timeout(&ir->completion, TIMEOUT);

	return 0;
}

/*
 * The iguana ir creates the carrier by busy spinning after each pulse or
 * space. This is counted in CPU cycles, with the CPU running at 24MHz. It is
 * broken down into 7-cycles and 4-cyles delays, with a preference for
 * 4-cycle delays.
 */
static int iguanair_set_tx_carrier(struct rc_dev *dev, uint32_t carrier)
{
	struct iguanair *ir = dev->priv;

	if (carrier < 25000 || carrier > 150000)
		return -EINVAL;

	mutex_lock(&ir->lock);

	if (carrier != ir->carrier) {
		uint32_t cycles, fours, sevens;

		ir->carrier = carrier;

		cycles = DIV_ROUND_CLOSEST(24000000, carrier * 2) -
							ir->cycle_overhead;

		/*  make up the the remainer of 4-cycle blocks */
		switch (cycles & 3) {
		case 0:
			sevens = 0;
			break;
		case 1:
			sevens = 3;
			break;
		case 2:
			sevens = 2;
			break;
		case 3:
			sevens = 1;
			break;
		}

		fours = (cycles - sevens * 7) / 4;

		/* magic happens here */
		ir->busy7 = (4 - sevens) * 2;
		ir->busy4 = 110 - fours;
	}

	mutex_unlock(&ir->lock);

	return carrier;
}

static int iguanair_set_tx_mask(struct rc_dev *dev, uint32_t mask)
{
	struct iguanair *ir = dev->priv;

	if (mask > 15)
		return 4;

	mutex_lock(&ir->lock);
	ir->channels = mask;
	mutex_unlock(&ir->lock);

	return 0;
}

static int iguanair_tx(struct rc_dev *dev, unsigned *txbuf, unsigned count)
{
	struct iguanair *ir = dev->priv;
	uint8_t space, *payload;
	unsigned i, size, rc;
	struct send_packet *packet;

	mutex_lock(&ir->lock);

	/* convert from us to carrier periods */
	for (i = size = 0; i < count; i++) {
		txbuf[i] = DIV_ROUND_CLOSEST(txbuf[i] * ir->carrier, 1000000);
		size += (txbuf[i] + 126) / 127;
	}

	packet = kmalloc(sizeof(*packet) + size, GFP_KERNEL);
	if (!packet) {
		rc = -ENOMEM;
		goto out;
	}

	if (size > ir->bufsize) {
		rc = -E2BIG;
		goto out;
	}

	packet->header.start = 0;
	packet->header.direction = DIR_OUT;
	packet->header.cmd = CMD_SEND;
	packet->length = size;
	packet->channels = ir->channels << 4;
	packet->busy7 = ir->busy7;
	packet->busy4 = ir->busy4;

	space = 0;
	payload = packet->payload;

	for (i = 0; i < count; i++) {
		unsigned periods = txbuf[i];

		while (periods > 127) {
			*payload++ = 127 | space;
			periods -= 127;
		}

		*payload++ = periods | space;
		space ^= 0x80;
	}

	if (ir->receiver_on) {
		rc = iguanair_receiver(ir, false);
		if (rc) {
			dev_warn(ir->dev, "disable receiver before transmit failed\n");
			goto out;
		}
	}

	ir->tx_overflow = false;

	INIT_COMPLETION(ir->completion);

	rc = iguanair_send(ir, packet, size + 8, NULL, NULL);

	if (rc == 0) {
		wait_for_completion_timeout(&ir->completion, TIMEOUT);
		if (ir->tx_overflow)
			rc = -EOVERFLOW;
	}

	ir->tx_overflow = false;

	if (ir->receiver_on) {
		if (iguanair_receiver(ir, true))
			dev_warn(ir->dev, "re-enable receiver after transmit failed\n");
	}

out:
	mutex_unlock(&ir->lock);
	kfree(packet);

	return rc;
}

static int iguanair_open(struct rc_dev *rdev)
{
	struct iguanair *ir = rdev->priv;
	int rc;

	mutex_lock(&ir->lock);

	usb_submit_urb(ir->urb_in, GFP_KERNEL);

	BUG_ON(ir->receiver_on);

	rc = iguanair_receiver(ir, true);
	if (rc == 0)
		ir->receiver_on = true;

	mutex_unlock(&ir->lock);

	return rc;
}

static void iguanair_close(struct rc_dev *rdev)
{
	struct iguanair *ir = rdev->priv;
	int rc;

	mutex_lock(&ir->lock);

	rc = iguanair_receiver(ir, false);
	ir->receiver_on = false;
	if (rc)
		dev_warn(ir->dev, "failed to disable receiver: %d\n", rc);

	usb_kill_urb(ir->urb_in);

	mutex_unlock(&ir->lock);
}

static int __devinit iguanair_probe(struct usb_interface *intf,
						const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct iguanair *ir;
	struct rc_dev *rc;
	int ret;
	struct usb_host_interface *idesc;

	ir = kzalloc(sizeof(*ir), GFP_KERNEL);
	rc = rc_allocate_device();
	if (!ir || !rc) {
		ret = ENOMEM;
		goto out;
	}

	ir->buf_in = usb_alloc_coherent(udev, MAX_PACKET_SIZE, GFP_ATOMIC,
								&ir->dma_in);
	ir->urb_in = usb_alloc_urb(0, GFP_KERNEL);

	if (!ir->buf_in || !ir->urb_in) {
		ret = ENOMEM;
		goto out;
	}

	idesc = intf->altsetting;

	if (idesc->desc.bNumEndpoints < 2) {
		ret = -ENODEV;
		goto out;
	}

	ir->rc = rc;
	ir->dev = &intf->dev;
	ir->udev = udev;
	ir->pipe_in = usb_rcvintpipe(udev,
				idesc->endpoint[0].desc.bEndpointAddress);
	ir->pipe_out = usb_sndintpipe(udev,
				idesc->endpoint[1].desc.bEndpointAddress);
	mutex_init(&ir->lock);
	init_completion(&ir->completion);

	ret = iguanair_get_features(ir);
	if (ret) {
		dev_warn(&intf->dev, "failed to get device features");
		goto out;
	}

	usb_fill_int_urb(ir->urb_in, ir->udev, ir->pipe_in, ir->buf_in,
		MAX_PACKET_SIZE, iguanair_rx, ir,
		idesc->endpoint[0].desc.bInterval);
	ir->urb_in->transfer_dma = ir->dma_in;
	ir->urb_in->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	snprintf(ir->name, sizeof(ir->name),
		"IguanaWorks USB IR Transceiver version %d.%d",
		ir->version[0], ir->version[1]);

	usb_make_path(ir->udev, ir->phys, sizeof(ir->phys));

	rc->input_name = ir->name;
	rc->input_phys = ir->phys;
	usb_to_input_id(ir->udev, &rc->input_id);
	rc->dev.parent = &intf->dev;
	rc->driver_type = RC_DRIVER_IR_RAW;
	rc->allowed_protos = RC_TYPE_ALL;
	rc->priv = ir;
	rc->open = iguanair_open;
	rc->close = iguanair_close;
	rc->s_tx_mask = iguanair_set_tx_mask;
	rc->s_tx_carrier = iguanair_set_tx_carrier;
	rc->tx_ir = iguanair_tx;
	rc->driver_name = DRIVER_NAME;
	rc->map_name = RC_MAP_EMPTY;

	iguanair_set_tx_carrier(rc, 38000);

	ret = rc_register_device(rc);
	if (ret < 0) {
		dev_err(&intf->dev, "failed to register rc device %d", ret);
		goto out;
	}

	usb_set_intfdata(intf, ir);

	dev_info(&intf->dev, "Registered %s", ir->name);

	return 0;
out:
	if (ir) {
		usb_free_urb(ir->urb_in);
		usb_free_coherent(udev, MAX_PACKET_SIZE, ir->buf_in,
								ir->dma_in);
	}
	rc_free_device(rc);
	kfree(ir);
	return ret;
}

static void __devexit iguanair_disconnect(struct usb_interface *intf)
{
	struct iguanair *ir = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);

	usb_kill_urb(ir->urb_in);
	usb_free_urb(ir->urb_in);
	usb_free_coherent(ir->udev, MAX_PACKET_SIZE, ir->buf_in, ir->dma_in);
	rc_unregister_device(ir->rc);
	kfree(ir);
}

static int iguanair_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct iguanair *ir = usb_get_intfdata(intf);
	int rc = 0;

	mutex_lock(&ir->lock);

	if (ir->receiver_on) {
		rc = iguanair_receiver(ir, false);
		if (rc)
			dev_warn(ir->dev, "failed to disable receiver for suspend\n");
	}

	mutex_unlock(&ir->lock);

	return rc;
}

static int iguanair_resume(struct usb_interface *intf)
{
	struct iguanair *ir = usb_get_intfdata(intf);
	int rc = 0;

	mutex_lock(&ir->lock);

	if (ir->receiver_on) {
		rc = iguanair_receiver(ir, true);
		if (rc)
			dev_warn(ir->dev, "failed to enable receiver after resume\n");
	}

	mutex_unlock(&ir->lock);

	return rc;
}

static const struct usb_device_id iguanair_table[] = {
	{ USB_DEVICE(0x1781, 0x0938) },
	{ }
};

static struct usb_driver iguanair_driver = {
	.name =	DRIVER_NAME,
	.probe = iguanair_probe,
	.disconnect = __devexit_p(iguanair_disconnect),
	.suspend = iguanair_suspend,
	.resume = iguanair_resume,
	.reset_resume = iguanair_resume,
	.id_table = iguanair_table
};

module_usb_driver(iguanair_driver);

MODULE_DESCRIPTION("IguanaWorks USB IR Transceiver");
MODULE_AUTHOR("Sean Young <sean@mess.org>");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(usb, iguanair_table);

