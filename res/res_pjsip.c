/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2013, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

#include "asterisk.h"

#include <pjsip.h>
/* Needed for SUBSCRIBE, NOTIFY, and PUBLISH method definitions */
#include <pjsip_simple.h>
#include <pjsip/sip_transaction.h>
#include <pj/timer.h>
#include <pjlib.h>
#include <pjmedia/errno.h>

#include "asterisk/res_pjsip.h"
#include "res_pjsip/include/res_pjsip_private.h"
#include "asterisk/linkedlists.h"
#include "asterisk/logger.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/astobj2.h"
#include "asterisk/module.h"
#include "asterisk/serializer.h"
#include "asterisk/threadpool.h"
#include "asterisk/taskprocessor.h"
#include "asterisk/uuid.h"
#include "asterisk/sorcery.h"
#include "asterisk/file.h"
#include "asterisk/cli.h"
#include "asterisk/res_pjsip_cli.h"
#include "asterisk/test.h"
#include "asterisk/res_pjsip_presence_xml.h"
#include "asterisk/res_pjproject.h"

/*** MODULEINFO
	<depend>pjproject</depend>
	<depend>res_pjproject</depend>
	<depend>res_sorcery_config</depend>
	<depend>res_sorcery_memory</depend>
	<depend>res_sorcery_astdb</depend>
	<use type="module">res_statsd</use>
	<support_level>core</support_level>
 ***/

/*** DOCUMENTATION
	<configInfo name="res_pjsip" language="en_US">
		<synopsis>SIP Resource using PJProject</synopsis>
		<configFile name="pjsip.conf">
			<configObject name="endpoint">
				<synopsis>Endpoint</synopsis>
				<description><para>
					The <emphasis>Endpoint</emphasis> is the primary configuration object.
					It contains the core SIP related options only, endpoints are <emphasis>NOT</emphasis>
					dialable entries of their own. Communication with another SIP device is
					accomplished via Addresses of Record (AoRs) which have one or more
					contacts associated with them. Endpoints <emphasis>NOT</emphasis> configured to
					use a <literal>transport</literal> will default to first transport found
					in <filename>pjsip.conf</filename> that matches its type.
					</para>
					<para>Example: An Endpoint has been configured with no transport.
					When it comes time to call an AoR, PJSIP will find the
					first transport that matches the type. A SIP URI of <literal>sip:5000@[11::33]</literal>
					will use the first IPv6 transport and try to send the request.
					</para>
					<para>If the anonymous endpoint identifier is in use an endpoint with the name
					"anonymous@domain" will be searched for as a last resort. If this is not found
					it will fall back to searching for "anonymous". If neither endpoints are found
					the anonymous endpoint identifier will not return an endpoint and anonymous
					calling will not be possible.
					</para>
				</description>
				<configOption name="100rel" default="yes">
					<synopsis>Allow support for RFC3262 provisional ACK tags</synopsis>
					<description>
						<enumlist>
							<enum name="no" />
							<enum name="required" />
							<enum name="yes" />
						</enumlist>
					</description>
				</configOption>
				<configOption name="aggregate_mwi" default="yes">
					<synopsis>Condense MWI notifications into a single NOTIFY.</synopsis>
					<description><para>When enabled, <replaceable>aggregate_mwi</replaceable> condenses message
					waiting notifications from multiple mailboxes into a single NOTIFY. If it is disabled,
					individual NOTIFYs are sent for each mailbox.</para></description>
				</configOption>
				<configOption name="allow">
					<synopsis>Media Codec(s) to allow</synopsis>
				</configOption>
				<configOption name="codec_prefs_incoming_offer">
					<synopsis>Codec negotiation prefs for incoming offers.</synopsis>
					<description>
						<para>
							This is a string that describes how the codecs
							specified on an incoming SDP offer (pending) are reconciled with the codecs specified
							on an endpoint (configured) before being sent to the Asterisk core.
							The string actually specifies 4 <literal>name:value</literal> pair parameters
							separated by commas. Whitespace is ignored and they may be specified in any order.
							Note that this option is reserved for future functionality.

						</para>
						<para>
							Parameters:
						</para>
						<enumlist>
							<enum name="prefer: &lt; pending | configured &gt;">
								<para>
								</para>
								<enumlist>
									<enum name="pending"><para>The codec list from the caller. (default)</para></enum>
									<enum name="configured"><para>The codec list from the endpoint.</para></enum>
								</enumlist>
							</enum>
							<enum name="operation : &lt; intersect | only_preferred | only_nonpreferred &gt;">
								<para>
								</para>
								<enumlist>
									<enum name="intersect"><para>Only common codecs with the preferred codecs first. (default)</para></enum>
									<enum name="only_preferred"><para>Use only the preferred codecs.</para></enum>
									<enum name="only_nonpreferred"><para>Use only the non-preferred codecs.</para></enum>
								</enumlist>
							</enum>
							<enum name="keep : &lt; all | first &gt;">
								<para>
								</para>
								<enumlist>
									<enum name="all"><para>After the operation, keep all codecs. (default)</para></enum>
									<enum name="first"><para>After the operation, keep only the first codec.</para></enum>
								</enumlist>
							</enum>
							<enum name="transcode : &lt; allow | prevent &gt;">
								<para>
								</para>
								<enumlist>
									<enum name="allow"><para>Allow transcoding. (default)</para></enum>
									<enum name="prevent"><para>Prevent transcoding.</para></enum>
								</enumlist>
							</enum>
						</enumlist>
						<para>
						</para>
						<example>
							codec_prefs_incoming_offer = prefer: pending, operation: intersect, keep: all, transcode: allow
						</example>
						<para>
							Prefer the codecs coming from the caller.  Use only the ones that are common.
							keeping the order of the preferred list. Keep all codecs in the result. Allow transcoding.
						</para>
					</description>
				</configOption>
				<configOption name="codec_prefs_outgoing_offer">
					<synopsis>Codec negotiation prefs for outgoing offers.</synopsis>
					<description>
						<para>
							This is a string that describes how the codecs specified in the topology that
							comes from the Asterisk core (pending) are reconciled with the codecs specified on an
							endpoint (configured) when sending an SDP offer.
							The string actually specifies 4 <literal>name:value</literal> pair parameters
							separated by commas. Whitespace is ignored and they may be specified in any order.
							Note that this option is reserved for future functionality.

						</para>
						<para>
							Parameters:
						</para>
						<enumlist>
							<enum name="prefer: &lt; pending | configured &gt;">
								<para>
								</para>
								<enumlist>
									<enum name="pending"><para>The codec list from the core. (default)</para></enum>
									<enum name="configured"><para>The codec list from the endpoint.</para></enum>
								</enumlist>
							</enum>
							<enum name="operation : &lt; union | intersect | only_preferred | only_nonpreferred &gt;">
								<para>
								</para>
								<enumlist>
									<enum name="union"><para>Merge the lists with the preferred codecs first. (default)</para></enum>
									<enum name="intersect"><para>Only common codecs with the preferred codecs first. (default)</para></enum>
									<enum name="only_preferred"><para>Use only the preferred codecs.</para></enum>
									<enum name="only_nonpreferred"><para>Use only the non-preferred codecs.</para></enum>
								</enumlist>
							</enum>
							<enum name="keep : &lt; all | first &gt;">
								<para>
								</para>
								<enumlist>
									<enum name="all"><para>After the operation, keep all codecs. (default)</para></enum>
									<enum name="first"><para>After the operation, keep only the first codec.</para></enum>
								</enumlist>
							</enum>
							<enum name="transcode : &lt; allow | prevent &gt;">
								<para>
								</para>
								<enumlist>
									<enum name="allow"><para>Allow transcoding. (default)</para></enum>
									<enum name="prevent"><para>Prevent transcoding.</para></enum>
								</enumlist>
							</enum>
						</enumlist>
						<para>
						</para>
						<example>
						codec_prefs_outgoing_offer = prefer: configured, operation: union, keep: first, transcode: prevent
						</example>
						<para>
						Prefer the codecs coming from the endpoint.  Merge them with the codecs from the core
						keeping the order of the preferred list. Keep only the first one. No transcoding allowed.
						</para>
					</description>
				</configOption>
				<configOption name="codec_prefs_incoming_answer">
					<synopsis>Codec negotiation prefs for incoming answers.</synopsis>
					<description>
						<para>
							This is a string that describes how the codecs specified in an incoming SDP answer
							(pending) are reconciled with the codecs specified on an endpoint (configured)
							when receiving an SDP answer.
							The string actually specifies 4 <literal>name:value</literal> pair parameters
							separated by commas. Whitespace is ignored and they may be specified in any order.
							Note that this option is reserved for future functionality.

						</para>
						<para>
							Parameters:
						</para>
						<enumlist>
							<enum name="prefer: &lt; pending | configured &gt;">
								<para>
								</para>
								<enumlist>
									<enum name="pending"><para>The codec list in the received SDP answer. (default)</para></enum>
									<enum name="configured"><para>The codec list from the endpoint.</para></enum>
								</enumlist>
							</enum>
							<enum name="operation : &lt; union | intersect | only_preferred | only_nonpreferred &gt;">
								<para>
								</para>
								<enumlist>
									<enum name="union"><para>Merge the lists with the preferred codecs first.</para></enum>
									<enum name="intersect"><para>Only common codecs with the preferred codecs first. (default)</para></enum>
									<enum name="only_preferred"><para>Use only the preferred codecs.</para></enum>
									<enum name="only_nonpreferred"><para>Use only the non-preferred codecs.</para></enum>
								</enumlist>
							</enum>
							<enum name="keep : &lt; all | first &gt;">
								<para>
								</para>
								<enumlist>
									<enum name="all"><para>After the operation, keep all codecs. (default)</para></enum>
									<enum name="first"><para>After the operation, keep only the first codec.</para></enum>
								</enumlist>
							</enum>
							<enum name="transcode : &lt; allow | prevent &gt;">
								<para>
								The transcode parameter is ignored when processing answers.
								</para>
							</enum>
						</enumlist>
						<para>
						</para>
						<example>
						codec_prefs_incoming_answer = keep: first
						</example>
						<para>
						Use the defaults but keep oinly the first codec.
						</para>
					</description>
				</configOption>
				<configOption name="codec_prefs_outgoing_answer">
					<synopsis>Codec negotiation prefs for outgoing answers.</synopsis>
					<description>
						<para>
							This is a string that describes how the codecs that come from the core (pending)
							are reconciled with the codecs specified on an endpoint (configured)
							when sending an SDP answer.
							The string actually specifies 4 <literal>name:value</literal> pair parameters
							separated by commas. Whitespace is ignored and they may be specified in any order.
							Note that this option is reserved for future functionality.

						</para>
						<para>
							Parameters:
						</para>
						<enumlist>
							<enum name="prefer: &lt; pending | configured &gt;">
								<para>
								</para>
								<enumlist>
									<enum name="pending"><para>The codec list that came from the core. (default)</para></enum>
									<enum name="configured"><para>The codec list from the endpoint.</para></enum>
								</enumlist>
							</enum>
							<enum name="operation : &lt; union | intersect | only_preferred | only_nonpreferred &gt;">
								<para>
								</para>
								<enumlist>
									<enum name="union"><para>Merge the lists with the preferred codecs first.</para></enum>
									<enum name="intersect"><para>Only common codecs with the preferred codecs first. (default)</para></enum>
									<enum name="only_preferred"><para>Use only the preferred codecs.</para></enum>
									<enum name="only_nonpreferred"><para>Use only the non-preferred codecs.</para></enum>
								</enumlist>
							</enum>
							<enum name="keep : &lt; all | first &gt;">
								<para>
								</para>
								<enumlist>
									<enum name="all"><para>After the operation, keep all codecs. (default)</para></enum>
									<enum name="first"><para>After the operation, keep only the first codec.</para></enum>
								</enumlist>
							</enum>
							<enum name="transcode : &lt; allow | prevent &gt;">
								<para>
								The transcode parameter is ignored when processing answers.
								</para>
							</enum>
						</enumlist>
						<para>
						</para>
						<example>
						codec_prefs_incoming_answer = keep: first
						</example>
						<para>
						Use the defaults but keep oinly the first codec.
						</para>
					</description>
				</configOption>
				<configOption name="allow_overlap" default="yes">
					<synopsis>Enable RFC3578 overlap dialing support.</synopsis>
				</configOption>
				<configOption name="aors">
					<synopsis>AoR(s) to be used with the endpoint</synopsis>
					<description><para>
						List of comma separated AoRs that the endpoint should be associated with.
					</para></description>
				</configOption>
				<configOption name="auth">
					<synopsis>Authentication Object(s) associated with the endpoint</synopsis>
					<description><para>
						This is a comma-delimited list of <replaceable>auth</replaceable> sections defined
						in <filename>pjsip.conf</filename> to be used to verify inbound connection attempts.
						</para><para>
						Endpoints without an authentication object
						configured will allow connections without verification.</para>
						<note><para>
						Using the same auth section for inbound and outbound
						authentication is not recommended.  There is a difference in
						meaning for an empty realm setting between inbound and outbound
						authentication uses.  See the auth realm description for details.
						</para></note>
					</description>
				</configOption>
				<configOption name="callerid">
					<synopsis>CallerID information for the endpoint</synopsis>
					<description><para>
						Must be in the format <literal>Name &lt;Number&gt;</literal>,
						or only <literal>&lt;Number&gt;</literal>.
					</para></description>
				</configOption>
				<configOption name="callerid_privacy">
					<synopsis>Default privacy level</synopsis>
					<description>
						<enumlist>
							<enum name="allowed_not_screened" />
							<enum name="allowed_passed_screen" />
							<enum name="allowed_failed_screen" />
							<enum name="allowed" />
							<enum name="prohib_not_screened" />
							<enum name="prohib_passed_screen" />
							<enum name="prohib_failed_screen" />
							<enum name="prohib" />
							<enum name="unavailable" />
						</enumlist>
					</description>
				</configOption>
				<configOption name="callerid_tag">
					<synopsis>Internal id_tag for the endpoint</synopsis>
				</configOption>
				<configOption name="context">
					<synopsis>Dialplan context for inbound sessions</synopsis>
				</configOption>
				<configOption name="direct_media_glare_mitigation" default="none">
					<synopsis>Mitigation of direct media (re)INVITE glare</synopsis>
					<description>
						<para>
						This setting attempts to avoid creating INVITE glare scenarios
						by disabling direct media reINVITEs in one direction thereby allowing
						designated servers (according to this option) to initiate direct
						media reINVITEs without contention and significantly reducing call
						setup time.
						</para>
						<para>
						A more detailed description of how this option functions can be found on
						the Asterisk wiki https://wiki.asterisk.org/wiki/display/AST/SIP+Direct+Media+Reinvite+Glare+Avoidance
						</para>
						<enumlist>
							<enum name="none" />
							<enum name="outgoing" />
							<enum name="incoming" />
						</enumlist>
					</description>
				</configOption>
				<configOption name="direct_media_method" default="invite">
					<synopsis>Direct Media method type</synopsis>
					<description>
						<para>Method for setting up Direct Media between endpoints.</para>
						<enumlist>
							<enum name="invite" />
							<enum name="reinvite">
								<para>Alias for the <literal>invite</literal> value.</para>
							</enum>
							<enum name="update" />
						</enumlist>
					</description>
				</configOption>
				<configOption name="trust_connected_line">
					<synopsis>Accept Connected Line updates from this endpoint</synopsis>
				</configOption>
				<configOption name="send_connected_line">
					<synopsis>Send Connected Line updates to this endpoint</synopsis>
				</configOption>
				<configOption name="connected_line_method" default="invite">
					<synopsis>Connected line method type</synopsis>
					<description>
						<para>Method used when updating connected line information.</para>
						<enumlist>
							<enum name="invite">
							<para>When set to <literal>invite</literal>, check the remote's Allow header and
							if UPDATE is allowed, send UPDATE instead of INVITE to avoid SDP
							renegotiation.  If UPDATE is not Allowed, send INVITE.</para>
							</enum>
							<enum name="reinvite">
								<para>Alias for the <literal>invite</literal> value.</para>
							</enum>
							<enum name="update">
							<para>If set to <literal>update</literal>, send UPDATE regardless of what the remote
							Allows. </para>
							</enum>
						</enumlist>
					</description>
				</configOption>
				<configOption name="direct_media" default="yes">
					<synopsis>Determines whether media may flow directly between endpoints.</synopsis>
				</configOption>
				<configOption name="disable_direct_media_on_nat" default="no">
					<synopsis>Disable direct media session refreshes when NAT obstructs the media session</synopsis>
				</configOption>
				<configOption name="disallow">
					<synopsis>Media Codec(s) to disallow</synopsis>
				</configOption>
				<configOption name="dtmf_mode" default="rfc4733">
					<synopsis>DTMF mode</synopsis>
					<description>
						<para>This setting allows to choose the DTMF mode for endpoint communication.</para>
						<enumlist>
							<enum name="rfc4733">
								<para>DTMF is sent out of band of the main audio stream.  This
								supercedes the older <emphasis>RFC-2833</emphasis> used within
								the older <literal>chan_sip</literal>.</para>
							</enum>
							<enum name="inband">
								<para>DTMF is sent as part of audio stream.</para>
							</enum>
							<enum name="info">
								<para>DTMF is sent as SIP INFO packets.</para>
							</enum>
							<enum name="auto">
								<para>DTMF is sent as RFC 4733 if the other side supports it or as INBAND if not.</para>
							</enum>
							<enum name="auto_info">
								<para>DTMF is sent as RFC 4733 if the other side supports it or as SIP INFO if not.</para>
							</enum>
						</enumlist>
					</description>
				</configOption>
				<configOption name="media_address">
					<synopsis>IP address used in SDP for media handling</synopsis>
					<description><para>
						At the time of SDP creation, the IP address defined here will be used as
						the media address for individual streams in the SDP.
					</para>
					<note><para>
						Be aware that the <literal>external_media_address</literal> option, set in Transport
						configuration, can also affect the final media address used in the SDP.
					</para></note>
					</description>
				</configOption>
				<configOption name="bind_rtp_to_media_address">
					<synopsis>Bind the RTP instance to the media_address</synopsis>
					<description><para>
						If media_address is specified, this option causes the RTP instance to be bound to the
						specified ip address which causes the packets to be sent from that address.
					</para>
					</description>
				</configOption>
				<configOption name="force_rport" default="yes">
					<synopsis>Force use of return port</synopsis>
				</configOption>
				<configOption name="ice_support" default="no">
					<synopsis>Enable the ICE mechanism to help traverse NAT</synopsis>
				</configOption>
				<configOption name="identify_by">
					<synopsis>Way(s) for the endpoint to be identified</synopsis>
					<description>
						<para>Endpoints and AORs can be identified in multiple ways.  This
						option is a comma separated list of methods the endpoint can be
						identified.
						</para>
						<note><para>
						This option controls both how an endpoint is matched for incoming
						traffic and also how an AOR is determined if a registration
						occurs.  You must list at least one method that also matches for
						AORs or the registration will fail.
						</para></note>
						<enumlist>
							<enum name="username">
								<para>Matches the endpoint or AOR ID based on the username
								and domain in the From header (or To header for AORs).  If
								an exact match on both username and domain/realm fails, the
								match is retried with just the username.
								</para>
							</enum>
							<enum name="auth_username">
								<para>Matches the endpoint or AOR ID based on the username
								and realm in the Authentication header.  If an exact match
								on both username and domain/realm fails, the match is
								retried with just the username.
								</para>
								<note><para>This method of identification has some security
								considerations because an Authentication header is not
								present on the first message of a dialog when digest
								authentication is used.  The client can't generate it until
								the server sends the challenge in a 401 response.  Since
								Asterisk normally sends a security event when an incoming
								request can't be matched to an endpoint, using this method
								requires that the security event be deferred until a request
								is received with the Authentication header and only
								generated if the username doesn't result in a match.  This
								may result in a delay before an attack is recognized.  You
								can control how many unmatched requests are received from
								a single ip address before a security event is generated
								using the <literal>unidentified_request</literal>
								parameters in the "global" configuration object.
								</para></note>
							</enum>
							<enum name="ip">
								<para>Matches the endpoint based on the source IP address.
								</para>
								<para>This method of identification is not configured here
								but simply allowed by this configuration option.  See the
								documentation for the <literal>identify</literal>
								configuration section for more details on this method of
								endpoint identification.
								</para>
							</enum>
							<enum name="header">
								<para>Matches the endpoint based on a configured SIP header
								value.
								</para>
								<para>This method of identification is not configured here
								but simply allowed by this configuration option.  See the
								documentation for the <literal>identify</literal>
								configuration section for more details on this method of
								endpoint identification.
								</para>
							</enum>
						</enumlist>
					</description>
				</configOption>
				<configOption name="redirect_method">
					<synopsis>How redirects received from an endpoint are handled</synopsis>
					<description><para>
						When a redirect is received from an endpoint there are multiple ways it can be handled.
						If this option is set to <literal>user</literal> the user portion of the redirect target
						is treated as an extension within the dialplan and dialed using a Local channel. If this option
						is set to <literal>uri_core</literal> the target URI is returned to the dialing application
						which dials it using the PJSIP channel driver and endpoint originally used. If this option is
						set to <literal>uri_pjsip</literal> the redirect occurs within chan_pjsip itself and is not exposed
						to the core at all. The <literal>uri_pjsip</literal> option has the benefit of being more efficient
						and also supporting multiple potential redirect targets. The con is that since redirection occurs
						within chan_pjsip redirecting information is not forwarded and redirection can not be
						prevented.
						</para>
						<enumlist>
							<enum name="user" />
							<enum name="uri_core" />
							<enum name="uri_pjsip" />
						</enumlist>
					</description>
				</configOption>
				<configOption name="mailboxes">
					<synopsis>NOTIFY the endpoint when state changes for any of the specified mailboxes</synopsis>
					<description><para>
						Asterisk will send unsolicited MWI NOTIFY messages to the endpoint when state
						changes happen for any of the specified mailboxes. More than one mailbox can be
						specified with a comma-delimited string. app_voicemail mailboxes must be specified
						as mailbox@context; for example: mailboxes=6001@default. For mailboxes provided by
						external sources, such as through the res_mwi_external module, you must specify
						strings supported by the external system.
					</para><para>
						For endpoints that SUBSCRIBE for MWI, use the <literal>mailboxes</literal> option in your AOR
						configuration.
					</para></description>
				</configOption>
				<configOption name="mwi_subscribe_replaces_unsolicited">
					<synopsis>An MWI subscribe will replace sending unsolicited NOTIFYs</synopsis>
				</configOption>
				<configOption name="voicemail_extension">
					<synopsis>The voicemail extension to send in the NOTIFY Message-Account header</synopsis>
				</configOption>
				<configOption name="moh_suggest" default="default">
					<synopsis>Default Music On Hold class</synopsis>
				</configOption>
				<configOption name="outbound_auth">
					<synopsis>Authentication object(s) used for outbound requests</synopsis>
					<description><para>
						This is a comma-delimited list of <replaceable>auth</replaceable>
						sections defined in <filename>pjsip.conf</filename> used to respond
						to outbound connection authentication challenges.</para>
						<note><para>
						Using the same auth section for inbound and outbound
						authentication is not recommended.  There is a difference in
						meaning for an empty realm setting between inbound and outbound
						authentication uses.  See the auth realm description for details.
						</para></note>
					</description>
				</configOption>
				<configOption name="outbound_proxy">
					<synopsis>Full SIP URI of the outbound proxy used to send requests</synopsis>
				</configOption>
				<configOption name="rewrite_contact">
					<synopsis>Allow Contact header to be rewritten with the source IP address-port</synopsis>
					<description><para>
						On inbound SIP messages from this endpoint, the Contact header or an
						appropriate Record-Route header will be changed to have the source IP
						address and port.  This option does not affect outbound messages sent to
						this endpoint.  This option helps servers communicate with endpoints
						that are behind NATs.  This option also helps reuse reliable transport
						connections such as TCP and TLS.
					</para></description>
				</configOption>
				<configOption name="rtp_ipv6" default="no">
					<synopsis>Allow use of IPv6 for RTP traffic</synopsis>
				</configOption>
				<configOption name="rtp_symmetric" default="no">
					<synopsis>Enforce that RTP must be symmetric</synopsis>
				</configOption>
				<configOption name="send_diversion" default="yes">
					<synopsis>Send the Diversion header, conveying the diversion
					information to the called user agent</synopsis>
				</configOption>
				<configOption name="send_history_info" default="no">
					<synopsis>Send the History-Info header, conveying the diversion
					information to the called and calling user agents</synopsis>
				</configOption>
				<configOption name="send_pai" default="no">
					<synopsis>Send the P-Asserted-Identity header</synopsis>
				</configOption>
				<configOption name="send_rpid" default="no">
					<synopsis>Send the Remote-Party-ID header</synopsis>
				</configOption>
				<configOption name="rpid_immediate" default="no">
					<synopsis>Immediately send connected line updates on unanswered incoming calls.</synopsis>
					<description>
						<para>When enabled, immediately send <emphasis>180 Ringing</emphasis>
						or <emphasis>183 Progress</emphasis> response messages to the
						caller if the connected line information is updated before
						the call is answered.  This can send a <emphasis>180 Ringing</emphasis>
						response before the call has even reached the far end.  The
						caller can start hearing ringback before the far end even gets
						the call.  Many phones tend to grab the first connected line
						information and refuse to update the display if it changes.  The
						first information is not likely to be correct if the call
						goes to an endpoint not under the control of this Asterisk
						box.</para>
						<para>When disabled, a connected line update must wait for
						another reason to send a message with the connected line
						information to the caller before the call is answered.  You can
						trigger the sending of the information by using an appropriate
						dialplan application such as <emphasis>Ringing</emphasis>.</para>
					</description>
				</configOption>
				<configOption name="timers_min_se" default="90">
					<synopsis>Minimum session timers expiration period</synopsis>
					<description><para>
						Minimum session timer expiration period. Time in seconds.
					</para></description>
				</configOption>
				<configOption name="timers" default="yes">
					<synopsis>Session timers for SIP packets</synopsis>
					<description>
						<enumlist>
							<enum name="no" />
							<enum name="yes" />
							<enum name="required" />
							<enum name="always" />
							<enum name="forced"><para>Alias of always</para></enum>
						</enumlist>
					</description>
				</configOption>
				<configOption name="timers_sess_expires" default="1800">
					<synopsis>Maximum session timer expiration period</synopsis>
					<description><para>
						Maximum session timer expiration period. Time in seconds.
					</para></description>
				</configOption>
				<configOption name="transport">
					<synopsis>Explicit transport configuration to use</synopsis>
					<description>
						<para>This will <emphasis>force</emphasis> the endpoint to use the
						specified transport configuration to send SIP messages.  You need
						to already know what kind of transport (UDP/TCP/IPv4/etc) the
						endpoint device will use.
						</para>
						<note><para>Not specifying a transport will select the first
						configured transport in <filename>pjsip.conf</filename> which is
						compatible with the URI we are trying to contact.
						</para></note>
						<warning><para>Transport configuration is not affected by reloads. In order to
						change transports, a full Asterisk restart is required</para></warning>
					</description>
				</configOption>
				<configOption name="trust_id_inbound" default="no">
					<synopsis>Accept identification information received from this endpoint</synopsis>
					<description><para>This option determines whether Asterisk will accept
					identification from the endpoint from headers such as P-Asserted-Identity
					or Remote-Party-ID header. This option applies both to calls originating from the
					endpoint and calls originating from Asterisk. If <literal>no</literal>, the
					configured Caller-ID from pjsip.conf will always be used as the identity for
					the endpoint.</para></description>
				</configOption>
				<configOption name="trust_id_outbound" default="no">
					<synopsis>Send private identification details to the endpoint.</synopsis>
					<description><para>This option determines whether res_pjsip will send private
					identification information to the endpoint. If <literal>no</literal>,
					private Caller-ID information will not be forwarded to the endpoint.
					"Private" in this case refers to any method of restricting identification.
					Example: setting <replaceable>callerid_privacy</replaceable> to any
					<literal>prohib</literal> variation.
					Example: If <replaceable>trust_id_inbound</replaceable> is set to
					<literal>yes</literal>, the presence of a <literal>Privacy: id</literal>
					header in a SIP request or response would indicate the identification
					provided in the request is private.</para></description>
				</configOption>
				<configOption name="type">
					<synopsis>Must be of type 'endpoint'.</synopsis>
				</configOption>
				<configOption name="use_ptime" default="no">
					<synopsis>Use Endpoint's requested packetization interval</synopsis>
				</configOption>
				<configOption name="use_avpf" default="no">
					<synopsis>Determines whether res_pjsip will use and enforce usage of AVPF for this
					endpoint.</synopsis>
					<description><para>
						If set to <literal>yes</literal>, res_pjsip will use the AVPF or SAVPF RTP
						profile for all media offers on outbound calls and media updates and will
						decline media offers not using the AVPF or SAVPF profile.
					</para><para>
						If set to <literal>no</literal>, res_pjsip will use the AVP or SAVP RTP
						profile for all media offers on outbound calls and media updates, and will
						decline media offers not using the AVP or SAVP profile.
					</para></description>
				</configOption>
				<configOption name="force_avp" default="no">
					<synopsis>Determines whether res_pjsip will use and enforce usage of AVP,
					regardless of the RTP profile in use for this endpoint.</synopsis>
					<description><para>
						If set to <literal>yes</literal>, res_pjsip will use the AVP, AVPF, SAVP, or
						SAVPF RTP profile for all media offers on outbound calls and media updates including
						those for DTLS-SRTP streams.
					</para><para>
						If set to <literal>no</literal>, res_pjsip will use the respective RTP profile
						depending on configuration.
					</para></description>
				</configOption>
				<configOption name="media_use_received_transport" default="no">
					<synopsis>Determines whether res_pjsip will use the media transport received in the
					offer SDP in the corresponding answer SDP.</synopsis>
					<description><para>
						If set to <literal>yes</literal>, res_pjsip will use the received media transport.
					</para><para>
						If set to <literal>no</literal>, res_pjsip will use the respective RTP profile
						depending on configuration.
					</para></description>
				</configOption>
				<configOption name="media_encryption" default="no">
					<synopsis>Determines whether res_pjsip will use and enforce usage of media encryption
					for this endpoint.</synopsis>
					<description>
						<enumlist>
							<enum name="no"><para>
								res_pjsip will offer no encryption and allow no encryption to be setup.
							</para></enum>
							<enum name="sdes"><para>
								res_pjsip will offer standard SRTP setup via in-SDP keys. Encrypted SIP
								transport should be used in conjunction with this option to prevent
								exposure of media encryption keys.
							</para></enum>
							<enum name="dtls"><para>
								res_pjsip will offer DTLS-SRTP setup.
							</para></enum>
						</enumlist>
					</description>
				</configOption>
				<configOption name="media_encryption_optimistic" default="no">
					<synopsis>Determines whether encryption should be used if possible but does not terminate the
					session if not achieved.</synopsis>
					<description><para>
						This option only applies if <replaceable>media_encryption</replaceable> is
						set to <literal>sdes</literal> or <literal>dtls</literal>.
					</para></description>
				</configOption>
				<configOption name="g726_non_standard" default="no">
					<synopsis>Force g.726 to use AAL2 packing order when negotiating g.726 audio</synopsis>
					<description><para>
						When set to "yes" and an endpoint negotiates g.726 audio then use g.726 for AAL2
						packing order instead of what is recommended by RFC3551. Since this essentially
						replaces the underlying 'g726' codec with 'g726aal2' then 'g726aal2' needs to be
						specified in the endpoint's allowed codec list.
					</para></description>
				</configOption>
				<configOption name="inband_progress" default="no">
					<synopsis>Determines whether chan_pjsip will indicate ringing using inband
						progress.</synopsis>
					<description><para>
						If set to <literal>yes</literal>, chan_pjsip will send a 183 Session Progress
						when told to indicate ringing and will immediately start sending ringing
						as audio.
					</para><para>
						If set to <literal>no</literal>, chan_pjsip will send a 180 Ringing when told
						to indicate ringing and will NOT send it as audio.
					</para></description>
				</configOption>
				<configOption name="call_group">
					<synopsis>The numeric pickup groups for a channel.</synopsis>
					<description><para>
						Can be set to a comma separated list of numbers or ranges between the values
						of 0-63 (maximum of 64 groups).
					</para></description>
				</configOption>
				<configOption name="pickup_group">
					<synopsis>The numeric pickup groups that a channel can pickup.</synopsis>
					<description><para>
						Can be set to a comma separated list of numbers or ranges between the values
						of 0-63 (maximum of 64 groups).
					</para></description>
				</configOption>
				<configOption name="named_call_group">
					<synopsis>The named pickup groups for a channel.</synopsis>
					<description><para>
						Can be set to a comma separated list of case sensitive strings limited by
						supported line length.
					</para></description>
				</configOption>
				<configOption name="named_pickup_group">
					<synopsis>The named pickup groups that a channel can pickup.</synopsis>
					<description><para>
						Can be set to a comma separated list of case sensitive strings limited by
						supported line length.
					</para></description>
				</configOption>
				<configOption name="device_state_busy_at" default="0">
					<synopsis>The number of in-use channels which will cause busy to be returned as device state</synopsis>
					<description><para>
						When the number of in-use channels for the endpoint matches the devicestate_busy_at setting the
						PJSIP channel driver will return busy as the device state instead of in use.
					</para></description>
				</configOption>
				<configOption name="t38_udptl" default="no">
					<synopsis>Whether T.38 UDPTL support is enabled or not</synopsis>
					<description><para>
						If set to yes T.38 UDPTL support will be enabled, and T.38 negotiation requests will be accepted
						and relayed.
					</para></description>
				</configOption>
				<configOption name="t38_udptl_ec" default="none">
					<synopsis>T.38 UDPTL error correction method</synopsis>
					<description>
						<enumlist>
							<enum name="none"><para>
								No error correction should be used.
							</para></enum>
							<enum name="fec"><para>
								Forward error correction should be used.
							</para></enum>
							<enum name="redundancy"><para>
								Redundancy error correction should be used.
							</para></enum>
						</enumlist>
					</description>
				</configOption>
				<configOption name="t38_udptl_maxdatagram" default="0">
					<synopsis>T.38 UDPTL maximum datagram size</synopsis>
					<description><para>
						This option can be set to override the maximum datagram of a remote endpoint for broken
						endpoints.
					</para></description>
				</configOption>
				<configOption name="fax_detect" default="no">
					<synopsis>Whether CNG tone detection is enabled</synopsis>
					<description><para>
						This option can be set to send the session to the fax extension when a CNG tone is
						detected.
					</para></description>
				</configOption>
				<configOption name="fax_detect_timeout">
					<synopsis>How long into a call before fax_detect is disabled for the call</synopsis>
					<description><para>
						The option determines how many seconds into a call before the
						fax_detect option is disabled for the call.  Setting the value
						to zero disables the timeout.
					</para></description>
				</configOption>
				<configOption name="t38_udptl_nat" default="no">
					<synopsis>Whether NAT support is enabled on UDPTL sessions</synopsis>
					<description><para>
						When enabled the UDPTL stack will send UDPTL packets to the source address of
						received packets.
					</para></description>
				</configOption>
				<configOption name="t38_udptl_ipv6" default="no">
					<synopsis>Whether IPv6 is used for UDPTL Sessions</synopsis>
					<description><para>
						When enabled the UDPTL stack will use IPv6.
					</para></description>
				</configOption>
				<configOption name="t38_bind_udptl_to_media_address" default="no">
					<synopsis>Bind the UDPTL instance to the media_adress</synopsis>
					<description><para>
						If media_address is specified, this option causes the UDPTL instance to be bound to
						the specified ip address which causes the packets to be sent from that address.
					</para></description>
				</configOption>
				<configOption name="tone_zone">
					<synopsis>Set which country's indications to use for channels created for this endpoint.</synopsis>
				</configOption>
				<configOption name="language">
					<synopsis>Set the default language to use for channels created for this endpoint.</synopsis>
				</configOption>
				<configOption name="one_touch_recording" default="no">
					<synopsis>Determines whether one-touch recording is allowed for this endpoint.</synopsis>
					<see-also>
						<ref type="configOption">record_on_feature</ref>
						<ref type="configOption">record_off_feature</ref>
					</see-also>
				</configOption>
				<configOption name="record_on_feature" default="automixmon">
					<synopsis>The feature to enact when one-touch recording is turned on.</synopsis>
					<description>
						<para>When an INFO request for one-touch recording arrives with a Record header set to "on", this
						feature will be enabled for the channel. The feature designated here can be any built-in
						or dynamic feature defined in features.conf.</para>
						<note><para>This setting has no effect if the endpoint's one_touch_recording option is disabled</para></note>
					</description>
					<see-also>
						<ref type="configOption">one_touch_recording</ref>
						<ref type="configOption">record_off_feature</ref>
					</see-also>
				</configOption>
				<configOption name="record_off_feature" default="automixmon">
					<synopsis>The feature to enact when one-touch recording is turned off.</synopsis>
					<description>
						<para>When an INFO request for one-touch recording arrives with a Record header set to "off", this
						feature will be enabled for the channel. The feature designated here can be any built-in
						or dynamic feature defined in features.conf.</para>
						<note><para>This setting has no effect if the endpoint's one_touch_recording option is disabled</para></note>
					</description>
					<see-also>
						<ref type="configOption">one_touch_recording</ref>
						<ref type="configOption">record_on_feature</ref>
					</see-also>
				</configOption>
				<configOption name="rtp_engine" default="asterisk">
					<synopsis>Name of the RTP engine to use for channels created for this endpoint</synopsis>
				</configOption>
				<configOption name="allow_transfer" default="yes">
					<synopsis>Determines whether SIP REFER transfers are allowed for this endpoint</synopsis>
				</configOption>
				<configOption name="user_eq_phone" default="no">
					<synopsis>Determines whether a user=phone parameter is placed into the request URI if the user is determined to be a phone number</synopsis>
				</configOption>
				<configOption name="moh_passthrough" default="no">
					<synopsis>Determines whether hold and unhold will be passed through using re-INVITEs with recvonly and sendrecv to the remote side</synopsis>
				</configOption>
				<configOption name="sdp_owner" default="-">
					<synopsis>String placed as the username portion of an SDP origin (o=) line.</synopsis>
				</configOption>
				<configOption name="sdp_session" default="Asterisk">
					<synopsis>String used for the SDP session (s=) line.</synopsis>
				</configOption>
				<configOption name="tos_audio">
					<synopsis>DSCP TOS bits for audio streams</synopsis>
					<description><para>
						See https://wiki.asterisk.org/wiki/display/AST/IP+Quality+of+Service for more information about QoS settings
					</para></description>
				</configOption>
				<configOption name="tos_video">
					<synopsis>DSCP TOS bits for video streams</synopsis>
					<description><para>
						See https://wiki.asterisk.org/wiki/display/AST/IP+Quality+of+Service for more information about QoS settings
					</para></description>
				</configOption>
				<configOption name="cos_audio">
					<synopsis>Priority for audio streams</synopsis>
					<description><para>
						See https://wiki.asterisk.org/wiki/display/AST/IP+Quality+of+Service for more information about QoS settings
					</para></description>
				</configOption>
				<configOption name="cos_video">
					<synopsis>Priority for video streams</synopsis>
					<description><para>
						See https://wiki.asterisk.org/wiki/display/AST/IP+Quality+of+Service for more information about QoS settings
					</para></description>
				</configOption>
				<configOption name="allow_subscribe" default="yes">
					<synopsis>Determines if endpoint is allowed to initiate subscriptions with Asterisk.</synopsis>
				</configOption>
				<configOption name="sub_min_expiry" default="60">
					<synopsis>The minimum allowed expiry time for subscriptions initiated by the endpoint.</synopsis>
				</configOption>
				<configOption name="from_user">
					<synopsis>Username to use in From header for requests to this endpoint.</synopsis>
				</configOption>
				<configOption name="mwi_from_user">
					<synopsis>Username to use in From header for unsolicited MWI NOTIFYs to this endpoint.</synopsis>
				</configOption>
				<configOption name="from_domain">
					<synopsis>Domain to user in From header for requests to this endpoint.</synopsis>
				</configOption>
				<configOption name="dtls_verify">
					<synopsis>Verify that the provided peer certificate is valid</synopsis>
					<description><para>
						This option only applies if <replaceable>media_encryption</replaceable> is
						set to <literal>dtls</literal>.
						</para><para>
						It can be one of the following values:
						</para><enumlist>
							<enum name="no"><para>
								meaning no verificaton is done.
							</para></enum>
							<enum name="fingerprint"><para>
								meaning to verify the remote fingerprint.
							</para></enum>
							<enum name="certificate"><para>
								meaning to verify the remote certificate.
							</para></enum>
							<enum name="yes"><para>
								meaning to verify both the remote fingerprint and certificate.
							</para></enum>
						</enumlist>
					</description>
				</configOption>
				<configOption name="dtls_rekey">
					<synopsis>Interval at which to renegotiate the TLS session and rekey the SRTP session</synopsis>
					<description><para>
						This option only applies if <replaceable>media_encryption</replaceable> is
						set to <literal>dtls</literal>.
					</para><para>
						If this is not set or the value provided is 0 rekeying will be disabled.
					</para></description>
				</configOption>
				<configOption name="dtls_auto_generate_cert" default="no">
					<synopsis>Whether or not to automatically generate an ephemeral X.509 certificate</synopsis>
					<description>
						<para>
							If enabled, Asterisk will generate an X.509 certificate for each DTLS session.
							This option only applies if <replaceable>media_encryption</replaceable> is set
							to <literal>dtls</literal>. This option will be automatically enabled if
							<literal>webrtc</literal> is enabled and <literal>dtls_cert_file</literal> is
							not specified.
						</para>
					</description>
				</configOption>
				<configOption name="dtls_cert_file">
					<synopsis>Path to certificate file to present to peer</synopsis>
					<description><para>
						This option only applies if <replaceable>media_encryption</replaceable> is
						set to <literal>dtls</literal>.
					</para></description>
				</configOption>
				<configOption name="dtls_private_key">
					<synopsis>Path to private key for certificate file</synopsis>
					<description><para>
						This option only applies if <replaceable>media_encryption</replaceable> is
						set to <literal>dtls</literal>.
					</para></description>
				</configOption>
				<configOption name="dtls_cipher">
					<synopsis>Cipher to use for DTLS negotiation</synopsis>
					<description><para>
						This option only applies if <replaceable>media_encryption</replaceable> is
						set to <literal>dtls</literal>.
					</para>
					<para>Many options for acceptable ciphers. See link for more:</para>
					<para>http://www.openssl.org/docs/apps/ciphers.html#CIPHER_STRINGS
					</para></description>
				</configOption>
				<configOption name="dtls_ca_file">
					<synopsis>Path to certificate authority certificate</synopsis>
					<description><para>
						This option only applies if <replaceable>media_encryption</replaceable> is
						set to <literal>dtls</literal>.
					</para></description>
				</configOption>
				<configOption name="dtls_ca_path">
					<synopsis>Path to a directory containing certificate authority certificates</synopsis>
					<description><para>
						This option only applies if <replaceable>media_encryption</replaceable> is
						set to <literal>dtls</literal>.
					</para></description>
				</configOption>
				<configOption name="dtls_setup">
					<synopsis>Whether we are willing to accept connections, connect to the other party, or both.</synopsis>
					<description>
						<para>
							This option only applies if <replaceable>media_encryption</replaceable> is
							set to <literal>dtls</literal>.
						</para>
						<enumlist>
							<enum name="active"><para>
								res_pjsip will make a connection to the peer.
							</para></enum>
							<enum name="passive"><para>
								res_pjsip will accept connections from the peer.
							</para></enum>
							<enum name="actpass"><para>
								res_pjsip will offer and accept connections from the peer.
							</para></enum>
						</enumlist>
					</description>
				</configOption>
				<configOption name="dtls_fingerprint">
					<synopsis>Type of hash to use for the DTLS fingerprint in the SDP.</synopsis>
					<description>
						<para>
							This option only applies if <replaceable>media_encryption</replaceable> is
							set to <literal>dtls</literal>.
						</para>
						<enumlist>
							<enum name="SHA-256"></enum>
							<enum name="SHA-1"></enum>
						</enumlist>
					</description>
				</configOption>
				<configOption name="srtp_tag_32">
					<synopsis>Determines whether 32 byte tags should be used instead of 80 byte tags.</synopsis>
					<description><para>
						This option only applies if <replaceable>media_encryption</replaceable> is
						set to <literal>sdes</literal> or <literal>dtls</literal>.
					</para></description>
				</configOption>
				<configOption name="set_var">
					<synopsis>Variable set on a channel involving the endpoint.</synopsis>
					<description><para>
						When a new channel is created using the endpoint set the specified
						variable(s) on that channel. For multiple channel variables specify
						multiple 'set_var'(s).
					</para></description>
				</configOption>
				<configOption name="message_context">
					<synopsis>Context to route incoming MESSAGE requests to.</synopsis>
					<description><para>
						If specified, incoming MESSAGE requests will be routed to the indicated
						dialplan context. If no <replaceable>message_context</replaceable> is
						specified, then the <replaceable>context</replaceable> setting is used.
					</para></description>
				</configOption>
				<configOption name="accountcode">
					<synopsis>An accountcode to set automatically on any channels created for this endpoint.</synopsis>
					<description><para>
						If specified, any channel created for this endpoint will automatically
						have this accountcode set on it.
					</para></description>
				</configOption>
				<configOption name="preferred_codec_only" default="no">
					<synopsis>Respond to a SIP invite with the single most preferred codec (DEPRECATED)</synopsis>
					<description><para>Respond to a SIP invite with the single most preferred codec
					rather than advertising all joint codec capabilities. This limits the other side's codec
					choice to exactly what we prefer.</para>
					<warning><para>This option has been deprecated in favor of
					<literal>incoming_call_offer_pref</literal>.  Setting both options is unsupported.</para>
					</warning>
					</description>
					<see-also>
						<ref type="configOption">incoming_call_offer_pref</ref>
					</see-also>
				</configOption>
				<configOption name="incoming_call_offer_pref" default="local">
					<synopsis>Preferences for selecting codecs for an incoming call.</synopsis>
					<description>
						<para>Based on this setting, a joint list of preferred codecs between those
						received in an incoming SDP offer (remote), and those specified in the
						endpoint's "allow" parameter (local) es created and is passed to the Asterisk
						core. </para>
						<note><para>This list will consist of only those codecs found in both lists.</para></note>
						<enumlist>
							<enum name="local"><para>
								Include all codecs in the local list that are also in the remote list
								preserving the local order.  (default).
							</para></enum>
							<enum name="local_first"><para>
								Include only the first codec in the local list that is also in the remote list.
							</para></enum>
							<enum name="remote"><para>
								Include all codecs in the remote list that are also in the local list
								preserving the remote order.
							</para></enum>
							<enum name="remote_first"><para>
								Include only the first codec in the remote list that is also in the local list.
							</para></enum>
						</enumlist>
					</description>
				</configOption>
				<configOption name="outgoing_call_offer_pref" default="remote_merge">
					<synopsis>Preferences for selecting codecs for an outgoing call.</synopsis>
					<description>
						<para>Based on this setting, a joint list of preferred codecs between
						those received from the Asterisk core (remote), and those specified in
						the endpoint's "allow" parameter (local) is created and is used to create
						the outgoing SDP offer.</para>
						<enumlist>
							<enum name="local"><para>
								Include all codecs in the local list that are also in the remote list
								preserving the local order.
							</para></enum>
							<enum name="local_merge"><para>
								Include all codecs in the local list preserving the local order.
							</para></enum>
							<enum name="local_first"><para>
								Include only the first codec in the local list.
							</para></enum>
							<enum name="remote"><para>
								Include all codecs in the remote list that are also in the local list
								preserving the remote order.
							</para></enum>
							<enum name="remote_merge"><para>
                                                                Include all codecs in the local list preserving the remote order. (default)
							</para></enum>
							<enum name="remote_first"><para>
								Include only the first codec in the remote list that is also in the local list.
							</para></enum>
						</enumlist>
					</description>
				</configOption>
				<configOption name="rtp_keepalive">
					<synopsis>Number of seconds between RTP comfort noise keepalive packets.</synopsis>
					<description><para>
						At the specified interval, Asterisk will send an RTP comfort noise frame. This may
						be useful for situations where Asterisk is behind a NAT or firewall and must keep
						a hole open in order to allow for media to arrive at Asterisk.
					</para></description>
				</configOption>
				<configOption name="rtp_timeout" default="0">
					<synopsis>Maximum number of seconds without receiving RTP (while off hold) before terminating call.</synopsis>
					<description><para>
						This option configures the number of seconds without RTP (while off hold) before
						considering a channel as dead. When the number of seconds is reached the underlying
						channel is hung up. By default this option is set to 0, which means do not check.
					</para></description>
				</configOption>
				<configOption name="rtp_timeout_hold" default="0">
					<synopsis>Maximum number of seconds without receiving RTP (while on hold) before terminating call.</synopsis>
					<description><para>
						This option configures the number of seconds without RTP (while on hold) before
						considering a channel as dead. When the number of seconds is reached the underlying
						channel is hung up. By default this option is set to 0, which means do not check.
					</para></description>
				</configOption>
				<configOption name="acl">
					<synopsis>List of IP ACL section names in acl.conf</synopsis>
					<description><para>
						This matches sections configured in <literal>acl.conf</literal>. The value is
						defined as a list of comma-delimited section names.
					</para></description>
				</configOption>
				<configOption name="deny">
					<synopsis>List of IP addresses to deny access from</synopsis>
					<description><para>
						The value is a comma-delimited list of IP addresses. IP addresses may
						have a subnet mask appended. The subnet mask may be written in either
						CIDR or dotted-decimal notation. Separate the IP address and subnet
						mask with a slash ('/')
					</para></description>
				</configOption>
				<configOption name="permit">
					<synopsis>List of IP addresses to permit access from</synopsis>
					<description><para>
						The value is a comma-delimited list of IP addresses. IP addresses may
						have a subnet mask appended. The subnet mask may be written in either
						CIDR or dotted-decimal notation. Separate the IP address and subnet
						mask with a slash ('/')
					</para></description>
				</configOption>
				<configOption name="contact_acl">
					<synopsis>List of Contact ACL section names in acl.conf</synopsis>
					<description><para>
						This matches sections configured in <literal>acl.conf</literal>. The value is
						defined as a list of comma-delimited section names.
					</para></description>
				</configOption>
				<configOption name="contact_deny">
					<synopsis>List of Contact header addresses to deny</synopsis>
					<description><para>
						The value is a comma-delimited list of IP addresses. IP addresses may
						have a subnet mask appended. The subnet mask may be written in either
						CIDR or dotted-decimal notation. Separate the IP address and subnet
						mask with a slash ('/')
					</para></description>
				</configOption>
				<configOption name="contact_permit">
					<synopsis>List of Contact header addresses to permit</synopsis>
					<description><para>
						The value is a comma-delimited list of IP addresses. IP addresses may
						have a subnet mask appended. The subnet mask may be written in either
						CIDR or dotted-decimal notation. Separate the IP address and subnet
						mask with a slash ('/')
					</para></description>
				</configOption>
				<configOption name="subscribe_context">
					<synopsis>Context for incoming MESSAGE requests.</synopsis>
					<description><para>
						If specified, incoming SUBSCRIBE requests will be searched for the matching
						extension in the indicated context.
						If no <replaceable>subscribe_context</replaceable> is specified,
						then the <replaceable>context</replaceable> setting is used.
					</para></description>
				</configOption>
				<configOption name="contact_user" default="">
					<synopsis>Force the user on the outgoing Contact header to this value.</synopsis>
					<description><para>
						On outbound requests, force the user portion of the Contact header to this value.
					</para></description>
				</configOption>
				<configOption name="asymmetric_rtp_codec" default="no">
					<synopsis>Allow the sending and receiving RTP codec to differ</synopsis>
					<description><para>
						When set to "yes" the codec in use for sending will be allowed to differ from
						that of the received one. PJSIP will not automatically switch the sending one
						to the receiving one.
					</para></description>
				</configOption>
				<configOption name="rtcp_mux" default="no">
					<synopsis>Enable RFC 5761 RTCP multiplexing on the RTP port</synopsis>
					<description><para>
						With this option enabled, Asterisk will attempt to negotiate the use of the "rtcp-mux"
						attribute on all media streams. This will result in RTP and RTCP being sent and received
						on the same port. This shifts the demultiplexing logic to the application rather than
						the transport layer. This option is useful when interoperating with WebRTC endpoints
						since they mandate this option's use.
					</para></description>
				</configOption>
				<configOption name="refer_blind_progress" default="yes">
					<synopsis>Whether to notifies all the progress details on blind transfer</synopsis>
					<description><para>
						Some SIP phones (Mitel/Aastra, Snom) expect a sip/frag "200 OK"
						after REFER has been accepted. If set to <literal>no</literal> then asterisk
						will not send the progress details, but immediately will send "200 OK".
					</para></description>
				</configOption>
				<configOption name="notify_early_inuse_ringing" default="no">
					<synopsis>Whether to notifies dialog-info 'early' on InUse&amp;Ringing state</synopsis>
					<description><para>
						Control whether dialog-info subscriptions get 'early' state
						on Ringing when already INUSE.
					</para></description>
				</configOption>
				<configOption name="max_audio_streams" default="1">
					<synopsis>The maximum number of allowed audio streams for the endpoint</synopsis>
					<description><para>
						This option enforces a limit on the maximum simultaneous negotiated audio
						streams allowed for the endpoint.
					</para></description>
				</configOption>
				<configOption name="max_video_streams" default="1">
					<synopsis>The maximum number of allowed video streams for the endpoint</synopsis>
					<description><para>
						This option enforces a limit on the maximum simultaneous negotiated video
						streams allowed for the endpoint.
					</para></description>
				</configOption>
				<configOption name="bundle" default="no">
					<synopsis>Enable RTP bundling</synopsis>
					<description><para>
						With this option enabled, Asterisk will attempt to negotiate the use of bundle.
						If negotiated this will result in multiple RTP streams being carried over the same
						underlying transport. Note that enabling bundle will also enable the rtcp_mux option.
					</para></description>
				</configOption>
				<configOption name="webrtc" default="no">
					<synopsis>Defaults and enables some options that are relevant to WebRTC</synopsis>
					<description><para>
						When set to "yes" this also enables the following values that are needed in
						order for basic WebRTC support to work: rtcp_mux, use_avpf, ice_support, and
						use_received_transport. The following configuration settings also get defaulted
						as follows:</para>
						<para>media_encryption=dtls</para>
						<para>dtls_auto_generate_cert=yes (if dtls_cert_file is not set)</para>
						<para>dtls_verify=fingerprint</para>
						<para>dtls_setup=actpass</para>
					</description>
				</configOption>
				<configOption name="incoming_mwi_mailbox">
					<synopsis>Mailbox name to use when incoming MWI NOTIFYs are received</synopsis>
					<description><para>
						If an MWI NOTIFY is received <emphasis>from</emphasis> this endpoint,
						this mailbox will be used when notifying other modules of MWI status
						changes.  If not set, incoming MWI NOTIFYs are ignored.
					</para></description>
				</configOption>
				<configOption name="follow_early_media_fork">
					<synopsis>Follow SDP forked media when To tag is different</synopsis>
					<description><para>
						On outgoing calls, if the UAS responds with different SDP attributes
						on subsequent 18X or 2XX responses (such as a port update) AND the
						To tag on the subsequent response is different than that on the previous
						one, follow it. This usually happens when the INVITE is forked to multiple
						UASs and more than one sends an SDP answer.
						</para>
						<note><para>
							This option must also be enabled in the <literal>system</literal>
							section for it to take effect here.
						</para></note>
					</description>
				</configOption>
				<configOption name="accept_multiple_sdp_answers" default="no">
					<synopsis>Accept multiple SDP answers on non-100rel responses</synopsis>
					<description><para>
						On outgoing calls, if the UAS responds with different SDP attributes
						on non-100rel 18X or 2XX responses (such as a port update) AND the
						To tag on the subsequent response is the same as that on the previous one,
						process the updated SDP.  This can happen when the UAS needs to change ports
						for some reason such as using a separate port for custom ringback.
						</para>
						<note><para>
							This option must also be enabled in the <literal>system</literal>
							section for it to take effect here.
						</para></note>
					</description>
				</configOption>
				<configOption name="suppress_q850_reason_headers" default="no">
					<synopsis>Suppress Q.850 Reason headers for this endpoint</synopsis>
					<description><para>
						Some devices can't accept multiple Reason headers and get confused
						when both 'SIP' and 'Q.850' Reason headers are received.  This
						option allows the 'Q.850' Reason header to be suppressed.</para>
					</description>
				</configOption>
				<configOption name="ignore_183_without_sdp" default="no">
					<synopsis>Do not forward 183 when it doesn't contain SDP</synopsis>
					<description><para>
						Certain SS7 internetworking scenarios can result in a 183
						to be generated for reasons other than early media.  Forwarding
						this 183 can cause loss of ringback tone.  This flag emulates
						the behavior of chan_sip and prevents these 183 responses from
						being forwarded.</para>
					</description>
				</configOption>
				<configOption name="stir_shaken" default="no">
					<synopsis>Enable STIR/SHAKEN support on this endpoint</synopsis>
					<description><para>
						Enable STIR/SHAKEN support on this endpoint. On incoming INVITEs,
						the Identity header will be checked for validity. On outgoing
						INVITEs, an Identity header will be added.</para>
					</description>
				</configOption>
				<configOption name="allow_unauthenticated_options" default="no">
					<synopsis>Skip authentication when receiving OPTIONS requests</synopsis>
					<description><para>
						RFC 3261 says that the response to an OPTIONS request MUST be the
						same had the request been an INVITE. Some UAs use OPTIONS requests
						like a 'ping' and the expectation is that they will return a
						200 OK.</para>
						<para>Enabling <literal>allow_unauthenticated_options</literal>
						will skip authentication of OPTIONS requests for the given
						endpoint.</para>
						<para>There are security implications to enabling this setting as
						it can allow information disclosure to occur - specifically, if
						enabled, an external party could enumerate and find the endpoint
						name by sending OPTIONS requests and examining the
						responses.</para>
					</description>
				</configOption>
			</configObject>
			<configObject name="auth">
				<synopsis>Authentication type</synopsis>
				<description><para>
					Authentication objects hold the authentication information for use
					by other objects such as <literal>endpoints</literal> or <literal>registrations</literal>.
					This also allows for multiple objects to use a single auth object. See
					the <literal>auth_type</literal> config option for password style choices.
				</para></description>
				<configOption name="auth_type" default="userpass">
					<synopsis>Authentication type</synopsis>
					<description><para>
						This option specifies which of the password style config options should be read
						when trying to authenticate an endpoint inbound request. If set to <literal>userpass</literal>
						then we'll read from the 'password' option. For <literal>md5</literal> we'll read
						from 'md5_cred'. If set to <literal>google_oauth</literal> then we'll read from the
						refresh_token/oauth_clientid/oauth_secret fields. The following values are valid:
						</para>
						<enumlist>
							<enum name="md5"/>
							<enum name="userpass"/>
							<enum name="google_oauth"/>
						</enumlist>
						<para>
						</para>
						<note>
							<para>
								This setting only describes whether the password is in
								plain text or has been pre-hashed with MD5.  It doesn't describe
								the acceptable digest algorithms we'll accept in a received
								challenge.
							</para>
						</note>
					</description>
				</configOption>
				<configOption name="nonce_lifetime" default="32">
					<synopsis>Lifetime of a nonce associated with this authentication config.</synopsis>
				</configOption>
				<configOption name="md5_cred" default="">
					<synopsis>MD5 Hash used for authentication.</synopsis>
					<description><para>
						Only used when auth_type is <literal>md5</literal>.
						As an alternative to specifying a plain text password,
						you can hash the username, realm and password
						together one time and place the hash value here.
						The input to the hash function must be in the
						following format:
						</para>
						<para>
						</para>
						<para>
						&lt;username&gt;:&lt;realm&gt;:&lt;password&gt;
						</para>
						<para>
						</para>
						<para>
						For incoming authentication (asterisk is the server),
						the realm must match either the realm set in this object
						or the <variable>default_realm</variable> set in in the
						<replaceable>global</replaceable> object.
						</para>
						<para>
						</para>
						<para>
						For outgoing authentication (asterisk is the UAC),
						the realm must match what the server will be sending
						in their WWW-Authenticate header.  It can't be blank
						unless you expect the server to be sending a blank
						realm in the header.  You can't use pre-hashed
						paswords with a wildcard auth object.
						You can generate the hash with the following shell
						command:
						</para>
						<para>
						</para>
						<para>
						$ echo -n "myname:myrealm:mypassword" | md5sum
						</para>
						<para>
						</para>
						<para>
						Note the '-n'.  You don't want a newline to be part
						of the hash.
					</para></description>
				</configOption>
				<configOption name="password">
					<synopsis>Plain text password used for authentication.</synopsis>
					<description><para>Only used when auth_type is <literal>userpass</literal>.</para></description>
				</configOption>
				<configOption name="refresh_token">
					<synopsis>OAuth 2.0 refresh token</synopsis>
				</configOption>
				<configOption name="oauth_clientid">
					<synopsis>OAuth 2.0 application's client id</synopsis>
				</configOption>
				<configOption name="oauth_secret">
					<synopsis>OAuth 2.0 application's secret</synopsis>
				</configOption>
				<configOption name="realm" default="">
					<synopsis>SIP realm for endpoint</synopsis>
					<description><para>
						For incoming authentication (asterisk is the UAS),
						this is the realm to be sent on WWW-Authenticate
						headers.  If not specified, the <replaceable>global</replaceable>
						object's <variable>default_realm</variable> will be used.
						</para>
						<para>
						</para>
						<para>
						For outgoing authentication (asterisk is the UAS), this
						must either be the realm the server is expected to send,
						or left blank or contain a single '*' to automatically
						use the realm sent by the server. If you have multiple
						auth object for an endpoint, the realm is also used to
						match the auth object to the realm the server sent.
						</para>
						<para>
						</para>
						<note>
						<para>
						Using the same auth section for inbound and outbound
						authentication is not recommended.  There is a difference in
						meaning for an empty realm setting between inbound and outbound
						authentication uses.
						</para>
						</note>
						<para>
						</para>
						<note>
							<para>
								If more than one auth object with the same realm or
								more than one wildcard auth object associated to
								an endpoint, we can only use the first one of
								each defined on the endpoint.
							</para>
						</note>
					</description>
				</configOption>
				<configOption name="type">
					<synopsis>Must be 'auth'</synopsis>
				</configOption>
				<configOption name="username">
					<synopsis>Username to use for account</synopsis>
				</configOption>
			</configObject>
			<configObject name="domain_alias">
				<synopsis>Domain Alias</synopsis>
				<description><para>
					Signifies that a domain is an alias. If the domain on a session is
					not found to match an AoR then this object is used to see if we have
					an alias for the AoR to which the endpoint is binding. This objects
					name as defined in configuration should be the domain alias and a
					config option is provided to specify the domain to be aliased.
				</para></description>
				<configOption name="type">
					<synopsis>Must be of type 'domain_alias'.</synopsis>
				</configOption>
				<configOption name="domain">
					<synopsis>Domain to be aliased</synopsis>
				</configOption>
			</configObject>
			<configObject name="transport">
				<synopsis>SIP Transport</synopsis>
				<description><para>
					<emphasis>Transports</emphasis>
					</para>
					<para>There are different transports and protocol derivatives
						supported by <literal>res_pjsip</literal>. They are in order of
						preference: UDP, TCP, and WebSocket (WS).</para>
					<note><para>Changes to transport configuration in pjsip.conf will only be
						effected on a complete restart of Asterisk. A module reload
						will not suffice.</para></note>
				</description>
				<configOption name="async_operations" default="1">
					<synopsis>Number of simultaneous Asynchronous Operations</synopsis>
				</configOption>
				<configOption name="bind">
					<synopsis>IP Address and optional port to bind to for this transport</synopsis>
				</configOption>
				<configOption name="ca_list_file">
					<synopsis>File containing a list of certificates to read (TLS ONLY, not WSS)</synopsis>
				</configOption>
				<configOption name="ca_list_path">
					<synopsis>Path to directory containing a list of certificates to read (TLS ONLY, not WSS)</synopsis>
				</configOption>
				<configOption name="cert_file">
					<synopsis>Certificate file for endpoint (TLS ONLY, not WSS)</synopsis>
					<description><para>
						A path to a .crt or .pem file can be provided.  However, only
						the certificate is read from the file, not the private key.
						The <literal>priv_key_file</literal> option must supply a
						matching key file.
					</para></description>
				</configOption>
				<configOption name="cipher">
					<synopsis>Preferred cryptography cipher names (TLS ONLY, not WSS)</synopsis>
					<description>
					<para>Comma separated list of cipher names or numeric equivalents.
						Numeric equivalents can be either decimal or hexadecimal (0xX).
					</para>
					<para>There are many cipher names.  Use the CLI command
						<literal>pjsip list ciphers</literal> to see a list of cipher
						names available for your installation.  See link for more:</para>
					<para>http://www.openssl.org/docs/apps/ciphers.html#CIPHER_SUITE_NAMES
					</para>
					</description>
				</configOption>
				<configOption name="domain">
					<synopsis>Domain the transport comes from</synopsis>
				</configOption>
				<configOption name="external_media_address">
					<synopsis>External IP address to use in RTP handling</synopsis>
					<description><para>
						When a request or response is sent out, if the destination of the
						message is outside the IP network defined in the option <literal>localnet</literal>,
						and the media address in the SDP is within the localnet network, then the
						media address in the SDP will be rewritten to the value defined for
						<literal>external_media_address</literal>.
					</para></description>
				</configOption>
				<configOption name="external_signaling_address">
					<synopsis>External address for SIP signalling</synopsis>
				</configOption>
				<configOption name="external_signaling_port" default="0">
					<synopsis>External port for SIP signalling</synopsis>
				</configOption>
				<configOption name="method">
					<synopsis>Method of SSL transport (TLS ONLY, not WSS)</synopsis>
					<description>
						<enumlist>
							<enum name="default">
								<para>The default as defined by PJSIP. This is currently TLSv1, but may change with future releases.</para>
							</enum>
							<enum name="unspecified">
								<para>This option is equivalent to setting 'default'</para>
							</enum>
							<enum name="tlsv1" />
							<enum name="tlsv1_1" />
							<enum name="tlsv1_2" />
							<enum name="sslv2" />
							<enum name="sslv3" />
							<enum name="sslv23" />
						</enumlist>
					</description>
				</configOption>
				<configOption name="local_net">
					<synopsis>Network to consider local (used for NAT purposes).</synopsis>
					<description><para>This must be in CIDR or dotted decimal format with the IP
					and mask separated with a slash ('/').</para></description>
				</configOption>
				<configOption name="password">
					<synopsis>Password required for transport</synopsis>
				</configOption>
				<configOption name="priv_key_file">
					<synopsis>Private key file (TLS ONLY, not WSS)</synopsis>
				</configOption>
				<configOption name="protocol" default="udp">
					<synopsis>Protocol to use for SIP traffic</synopsis>
					<description>
						<enumlist>
							<enum name="udp" />
							<enum name="tcp" />
							<enum name="tls" />
							<enum name="ws" />
							<enum name="wss" />
							<enum name="flow" />
						</enumlist>
					</description>
				</configOption>
				<configOption name="require_client_cert" default="false">
					<synopsis>Require client certificate (TLS ONLY, not WSS)</synopsis>
				</configOption>
				<configOption name="type">
					<synopsis>Must be of type 'transport'.</synopsis>
				</configOption>
				<configOption name="verify_client" default="false">
					<synopsis>Require verification of client certificate (TLS ONLY, not WSS)</synopsis>
				</configOption>
				<configOption name="verify_server" default="false">
					<synopsis>Require verification of server certificate (TLS ONLY, not WSS)</synopsis>
				</configOption>
				<configOption name="tos" default="false">
					<synopsis>Enable TOS for the signalling sent over this transport</synopsis>
					<description>
					<para>See <literal>https://wiki.asterisk.org/wiki/display/AST/IP+Quality+of+Service</literal>
					for more information on this parameter.</para>
					<note><para>This option does not apply to the <replaceable>ws</replaceable>
					or the <replaceable>wss</replaceable> protocols.</para></note>
					</description>
				</configOption>
				<configOption name="cos" default="false">
					<synopsis>Enable COS for the signalling sent over this transport</synopsis>
					<description>
					<para>See <literal>https://wiki.asterisk.org/wiki/display/AST/IP+Quality+of+Service</literal>
					for more information on this parameter.</para>
					<note><para>This option does not apply to the <replaceable>ws</replaceable>
					or the <replaceable>wss</replaceable> protocols.</para></note>
					</description>
				</configOption>
				<configOption name="websocket_write_timeout">
					<synopsis>The timeout (in milliseconds) to set on WebSocket connections.</synopsis>
					<description>
						<para>If a websocket connection accepts input slowly, the timeout
						for writes to it can be increased to keep it from being disconnected.
						Value is in milliseconds; default is 100 ms.</para>
					</description>
				</configOption>
				<configOption name="allow_reload" default="no">
					<synopsis>Allow this transport to be reloaded.</synopsis>
					<description>
						<para>Allow this transport to be reloaded when res_pjsip is reloaded.
						This option defaults to "no" because reloading a transport may disrupt
						in-progress calls.</para>
					</description>
				</configOption>
				<configOption name="symmetric_transport" default="no">
					<synopsis>Use the same transport for outgoing requests as incoming ones.</synopsis>
					<description>
						<para>When a request from a dynamic contact
							comes in on a transport with this option set to 'yes',
							the transport name will be saved and used for subsequent
							outgoing requests like OPTIONS, NOTIFY and INVITE.  It's
							saved as a contact uri parameter named 'x-ast-txp' and will
							display with the contact uri in CLI, AMI, and ARI output.
							On the outgoing request, if a transport wasn't explicitly
							set on the endpoint AND the request URI is not a hostname,
							the saved transport will be used and the 'x-ast-txp'
							parameter stripped from the outgoing packet.
						</para>
					</description>
				</configOption>
			</configObject>
			<configObject name="contact">
				<synopsis>A way of creating an aliased name to a SIP URI</synopsis>
				<description><para>
					Contacts are a way to hide SIP URIs from the dialplan directly.
					They are also used to make a group of contactable parties when
					in use with <literal>AoR</literal> lists.
				</para></description>
				<configOption name="type">
					<synopsis>Must be of type 'contact'.</synopsis>
				</configOption>
				<configOption name="uri">
					<synopsis>SIP URI to contact peer</synopsis>
				</configOption>
				<configOption name="expiration_time">
					<synopsis>Time to keep alive a contact</synopsis>
					<description><para>
						Time to keep alive a contact. String style specification.
					</para></description>
				</configOption>
				<configOption name="qualify_frequency" default="0">
					<synopsis>Interval at which to qualify a contact</synopsis>
					<description><para>
						Interval between attempts to qualify the contact for reachability.
						If <literal>0</literal> never qualify. Time in seconds.
					</para></description>
				</configOption>
				<configOption name="qualify_timeout" default="3.0">
					<synopsis>Timeout for qualify</synopsis>
					<description><para>
						If the contact doesn't respond to the OPTIONS request before the timeout,
						the contact is marked unavailable.
						If <literal>0</literal> no timeout. Time in fractional seconds.
					</para></description>
				</configOption>
				<configOption name="authenticate_qualify">
					<synopsis>Authenticates a qualify challenge response if needed</synopsis>
					<description>
						<para>If true and a qualify request receives a challenge response then
						authentication is attempted before declaring the contact available.
						</para>
						<note><para>This option does nothing as we will always complete
						the challenge response authentication if the qualify request is
						challenged.
						</para></note>
					</description>
				</configOption>
				<configOption name="outbound_proxy">
					<synopsis>Outbound proxy used when sending OPTIONS request</synopsis>
					<description><para>
						If set the provided URI will be used as the outbound proxy when an
						OPTIONS request is sent to a contact for qualify purposes.
					</para></description>
				</configOption>
				<configOption name="path">
					<synopsis>Stored Path vector for use in Route headers on outgoing requests.</synopsis>
				</configOption>
				<configOption name="user_agent">
					<synopsis>User-Agent header from registration.</synopsis>
					<description><para>
						The User-Agent is automatically stored based on data present in incoming SIP
						REGISTER requests and is not intended to be configured manually.
					</para></description>
				</configOption>
				<configOption name="endpoint">
					<synopsis>Endpoint name</synopsis>
					<description><para>
						The name of the endpoint this contact belongs to
					</para></description>
				</configOption>
				<configOption name="reg_server">
					<synopsis>Asterisk Server name</synopsis>
					<description><para>
						Asterisk Server name on which SIP endpoint registered.
					</para></description>
				</configOption>
				<configOption name="via_addr">
					<synopsis>IP-address of the last Via header from registration.</synopsis>
					<description><para>
						The last Via header should contain the address of UA which sent the request.
						The IP-address of the last Via header is automatically stored based on data present
						in incoming SIP REGISTER requests and is not intended to be configured manually.
					</para></description>
				</configOption>
				<configOption name="via_port">
					<synopsis>IP-port of the last Via header from registration.</synopsis>
					<description><para>
						The IP-port of the last Via header is automatically stored based on data present
						in incoming SIP REGISTER requests and is not intended to be configured manually.
					</para></description>
				</configOption>
				<configOption name="call_id">
					<synopsis>Call-ID header from registration.</synopsis>
					<description><para>
						The Call-ID header is automatically stored based on data present
						in incoming SIP REGISTER requests and is not intended to be configured manually.
					</para></description>
				</configOption>
				<configOption name="prune_on_boot">
					<synopsis>A contact that cannot survive a restart/boot.</synopsis>
					<description><para>
						The option is set if the incoming SIP REGISTER contact is rewritten
						on a reliable transport and is not intended to be configured manually.
					</para></description>
				</configOption>
			</configObject>
			<configObject name="aor">
				<synopsis>The configuration for a location of an endpoint</synopsis>
				<description><para>
					An AoR is what allows Asterisk to contact an endpoint via res_pjsip. If no
					AoRs are specified, an endpoint will not be reachable by Asterisk.
					Beyond that, an AoR has other uses within Asterisk, such as inbound
					registration.
					</para><para>
					An <literal>AoR</literal> is a way to allow dialing a group
					of <literal>Contacts</literal> that all use the same
					<literal>endpoint</literal> for calls.
					</para><para>
					This can be used as another way of grouping a list of contacts to dial
					rather than specifying them each directly when dialing via the dialplan.
					This must be used in conjunction with the <literal>PJSIP_DIAL_CONTACTS</literal>.
					</para><para>
					Registrations: For Asterisk to match an inbound registration to an endpoint,
					the AoR object name must match the user portion of the SIP URI in the "To:"
					header of the inbound SIP registration. That will usually be equivalent
					to the "user name" set in your hard or soft phones configuration.
				</para></description>
				<configOption name="contact">
					<synopsis>Permanent contacts assigned to AoR</synopsis>
					<description><para>
						Contacts specified will be called whenever referenced
						by <literal>chan_pjsip</literal>.
						</para><para>
						Use a separate "contact=" entry for each contact required. Contacts
						are specified using a SIP URI.
					</para></description>
				</configOption>
				<configOption name="default_expiration" default="3600">
					<synopsis>Default expiration time in seconds for contacts that are dynamically bound to an AoR.</synopsis>
				</configOption>
				<configOption name="mailboxes">
					<synopsis>Allow subscriptions for the specified mailbox(es)</synopsis>
					<description><para>This option applies when an external entity subscribes to an AoR
						for Message Waiting Indications. The mailboxes specified will be subscribed to.
						More than one mailbox can be specified with a comma-delimited string.
						app_voicemail mailboxes must be specified as mailbox@context;
						for example: mailboxes=6001@default. For mailboxes provided by external sources,
						such as through the res_mwi_external module, you must specify strings supported by
						the external system.
					</para><para>
						For endpoints that cannot SUBSCRIBE for MWI, you can set the <literal>mailboxes</literal> option in your
						endpoint configuration section to enable unsolicited MWI NOTIFYs to the endpoint.
					</para></description>
				</configOption>
				<configOption name="voicemail_extension">
					<synopsis>The voicemail extension to send in the NOTIFY Message-Account header</synopsis>
				</configOption>
				<configOption name="maximum_expiration" default="7200">
					<synopsis>Maximum time to keep an AoR</synopsis>
					<description><para>
						Maximum time to keep a peer with explicit expiration. Time in seconds.
					</para></description>
				</configOption>
				<configOption name="max_contacts" default="0">
					<synopsis>Maximum number of contacts that can bind to an AoR</synopsis>
					<description><para>
						Maximum number of contacts that can associate with this AoR. This value does
						not affect the number of contacts that can be added with the "contact" option.
						It only limits contacts added through external interaction, such as
						registration.
						</para>
						<note><para>The <replaceable>rewrite_contact</replaceable> option
						registers the source address as the contact address to help with
						NAT and reusing connection oriented transports such as TCP and
						TLS.  Unfortunately, refreshing a registration may register a
						different contact address and exceed
						<replaceable>max_contacts</replaceable>.  The
						<replaceable>remove_existing</replaceable> and
						<replaceable>remove_unavailable</replaceable> options can help by
						removing either the soonest to expire or unavailable contact(s) over
						<replaceable>max_contacts</replaceable> which is likely the
						old <replaceable>rewrite_contact</replaceable> contact source
						address being refreshed.
						</para></note>
						<note><para>This should be set to <literal>1</literal> and
						<replaceable>remove_existing</replaceable> set to <literal>yes</literal> if you
						wish to stick with the older <literal>chan_sip</literal> behaviour.
						</para></note>
					</description>
				</configOption>
				<configOption name="minimum_expiration" default="60">
					<synopsis>Minimum keep alive time for an AoR</synopsis>
					<description><para>
						Minimum time to keep a peer with an explicit expiration. Time in seconds.
					</para></description>
				</configOption>
				<configOption name="remove_existing" default="no">
					<synopsis>Determines whether new contacts replace existing ones.</synopsis>
					<description><para>
						On receiving a new registration to the AoR should it remove enough
						existing contacts not added or updated by the registration to
						satisfy <replaceable>max_contacts</replaceable>?  Any removed
						contacts will expire the soonest.
						</para>
						<note><para>The <replaceable>rewrite_contact</replaceable> option
						registers the source address as the contact address to help with
						NAT and reusing connection oriented transports such as TCP and
						TLS.  Unfortunately, refreshing a registration may register a
						different contact address and exceed
						<replaceable>max_contacts</replaceable>.  The
						<replaceable>remove_existing</replaceable> option can help by
						removing the soonest to expire contact(s) over
						<replaceable>max_contacts</replaceable> which is likely the
						old <replaceable>rewrite_contact</replaceable> contact source
						address being refreshed.
						</para></note>
						<note><para>This should be set to <literal>yes</literal> and
						<replaceable>max_contacts</replaceable> set to <literal>1</literal> if you
						wish to stick with the older <literal>chan_sip</literal> behaviour.
						</para></note>
					</description>
				</configOption>
				<configOption name="remove_unavailable" default="no">
					<synopsis>Determines whether new contacts should replace unavailable ones.</synopsis>
					<description><para>
						The effect of this setting depends on the setting of
						<replaceable>remove_existing</replaceable>.</para>
						<para>If <replaceable>remove_existing</replaceable> is set to
						<literal>no</literal> (default), setting remove_unavailable to
						<literal>yes</literal> will remove only unavailable contacts that exceed
						<replaceable>max_contacts</replaceable>	to allow an incoming
						REGISTER to complete sucessfully.</para>
						<para>If <replaceable>remove_existing</replaceable> is set to
						<literal>yes</literal>, setting remove_unavailable to
						<literal>yes</literal> will prioritize unavailable contacts for removal
						instead of just removing the contact that expires the soonest.</para>
						<note><para>See <replaceable>remove_existing</replaceable> and
						<replaceable>max_contacts</replaceable> for further information about how
						these 3 settings interact.
						</para></note>
					</description>
				</configOption>
				<configOption name="type">
					<synopsis>Must be of type 'aor'.</synopsis>
				</configOption>
				<configOption name="qualify_frequency" default="0">
					<synopsis>Interval at which to qualify an AoR</synopsis>
					<description><para>
						Interval between attempts to qualify the AoR for reachability.
						If <literal>0</literal> never qualify. Time in seconds.
					</para></description>
				</configOption>
				<configOption name="qualify_timeout" default="3.0">
					<synopsis>Timeout for qualify</synopsis>
					<description><para>
						If the contact doesn't respond to the OPTIONS request before the timeout,
						the contact is marked unavailable.
						If <literal>0</literal> no timeout. Time in fractional seconds.
					</para></description>
				</configOption>
				<configOption name="authenticate_qualify">
					<synopsis>Authenticates a qualify challenge response if needed</synopsis>
					<description>
						<para>If true and a qualify request receives a challenge response then
						authentication is attempted before declaring the contact available.
						</para>
						<note><para>This option does nothing as we will always complete
						the challenge response authentication if the qualify request is
						challenged.
						</para></note>
					</description>
				</configOption>
				<configOption name="outbound_proxy">
					<synopsis>Outbound proxy used when sending OPTIONS request</synopsis>
					<description><para>
						If set the provided URI will be used as the outbound proxy when an
						OPTIONS request is sent to a contact for qualify purposes.
					</para></description>
				</configOption>
				<configOption name="support_path">
					<synopsis>Enables Path support for REGISTER requests and Route support for other requests.</synopsis>
					<description><para>
						When this option is enabled, the Path headers in register requests will be saved
						and its contents will be used in Route headers for outbound out-of-dialog requests
						and in Path headers for outbound 200 responses. Path support will also be indicated
						in the Supported header.
					</para></description>
				</configOption>
			</configObject>
			<configObject name="system">
				<synopsis>Options that apply to the SIP stack as well as other system-wide settings</synopsis>
				<description><para>
					The settings in this section are global. In addition to being global, the values will
					not be re-evaluated when a reload is performed. This is because the values must be set
					before the SIP stack is initialized. The only way to reset these values is to either
					restart Asterisk, or unload res_pjsip.so and then load it again.
				</para></description>
				<configOption name="timer_t1" default="500">
					<synopsis>Set transaction timer T1 value (milliseconds).</synopsis>
					<description><para>
						Timer T1 is the base for determining how long to wait before retransmitting
						requests that receive no response when using an unreliable transport (e.g. UDP).
						For more information on this timer, see RFC 3261, Section 17.1.1.1.
					</para></description>
				</configOption>
				<configOption name="timer_b" default="32000">
					<synopsis>Set transaction timer B value (milliseconds).</synopsis>
					<description><para>
						Timer B determines the maximum amount of time to wait after sending an INVITE
						request before terminating the transaction. It is recommended that this be set
						to 64 * Timer T1, but it may be set higher if desired. For more information on
						this timer, see RFC 3261, Section 17.1.1.1.
					</para></description>
				</configOption>
				<configOption name="compact_headers" default="no">
					<synopsis>Use the short forms of common SIP header names.</synopsis>
				</configOption>
				<configOption name="threadpool_initial_size" default="0">
					<synopsis>Initial number of threads in the res_pjsip threadpool.</synopsis>
				</configOption>
				<configOption name="threadpool_auto_increment" default="5">
					<synopsis>The amount by which the number of threads is incremented when necessary.</synopsis>
				</configOption>
				<configOption name="threadpool_idle_timeout" default="60">
					<synopsis>Number of seconds before an idle thread should be disposed of.</synopsis>
				</configOption>
				<configOption name="threadpool_max_size" default="0">
					<synopsis>Maximum number of threads in the res_pjsip threadpool.
					A value of 0 indicates no maximum.</synopsis>
				</configOption>
				<configOption name="disable_tcp_switch" default="yes">
					<synopsis>Disable automatic switching from UDP to TCP transports.</synopsis>
					<description><para>
						Disable automatic switching from UDP to TCP transports if outgoing
						request is too large.  See RFC 3261 section 18.1.1.
					</para></description>
				</configOption>
				<configOption name="follow_early_media_fork">
					<synopsis>Follow SDP forked media when To tag is different</synopsis>
					<description><para>
						On outgoing calls, if the UAS responds with different SDP attributes
						on subsequent 18X or 2XX responses (such as a port update) AND the
						To tag on the subsequent response is different than that on the previous
						one, follow it.
						</para>
						<note><para>
							This option must also be enabled on endpoints that require
							this functionality.
						</para></note>
					</description>
				</configOption>
				<configOption name="accept_multiple_sdp_answers">
					<synopsis>Follow SDP forked media when To tag is the same</synopsis>
					<description><para>
						On outgoing calls, if the UAS responds with different SDP attributes
						on non-100rel 18X or 2XX responses (such as a port update) AND the
						To tag on the subsequent response is the same as that on the previous one,
						process the updated SDP.
						</para>
						<note><para>
							This option must also be enabled on endpoints that require
							this functionality.
						</para></note>
					</description>
				</configOption>
				<configOption name="disable_rport" default="no">
					<synopsis>Disable the use of rport in outgoing requests.</synopsis>
					<description><para>
						Remove "rport" parameter from the outgoing requests.
					</para></description>
				</configOption>
				<configOption name="type">
					<synopsis>Must be of type 'system' UNLESS the object name is 'system'.</synopsis>
				</configOption>
			</configObject>
			<configObject name="global">
				<synopsis>Options that apply globally to all SIP communications</synopsis>
				<description><para>
					The settings in this section are global. Unlike options in the <literal>system</literal>
					section, these options can be refreshed by performing a reload.
				</para></description>
				<configOption name="max_forwards" default="70">
					<synopsis>Value used in Max-Forwards header for SIP requests.</synopsis>
				</configOption>
				<configOption name="keep_alive_interval" default="90">
					<synopsis>The interval (in seconds) to send keepalives to active connection-oriented transports.</synopsis>
				</configOption>
				<configOption name="contact_expiration_check_interval" default="30">
					<synopsis>The interval (in seconds) to check for expired contacts.</synopsis>
				</configOption>
				<configOption name="disable_multi_domain" default="no">
					<synopsis>Disable Multi Domain support</synopsis>
					<description><para>
						If disabled it can improve realtime performance by reducing the number of database requests.
					</para></description>
				</configOption>
				<configOption name="max_initial_qualify_time" default="0">
					<synopsis>The maximum amount of time from startup that qualifies should be attempted on all contacts.
					If greater than the qualify_frequency for an aor, qualify_frequency will be used instead.</synopsis>
				</configOption>
				<configOption name="unidentified_request_period" default="5">
					<synopsis>The number of seconds over which to accumulate unidentified requests.</synopsis>
					<description><para>
					If <literal>unidentified_request_count</literal> unidentified requests are received
					during <literal>unidentified_request_period</literal>, a security event will be generated.
					</para></description>
				</configOption>
				<configOption name="unidentified_request_count" default="5">
					<synopsis>The number of unidentified requests from a single IP to allow.</synopsis>
					<description><para>
					If <literal>unidentified_request_count</literal> unidentified requests are received
					during <literal>unidentified_request_period</literal>, a security event will be generated.
					</para></description>
				</configOption>
				<configOption name="unidentified_request_prune_interval" default="30">
					<synopsis>The interval at which unidentified requests are older than
					twice the unidentified_request_period are pruned.</synopsis>
				</configOption>
				<configOption name="type">
					<synopsis>Must be of type 'global' UNLESS the object name is 'global'.</synopsis>
				</configOption>
				<configOption name="user_agent" default="Asterisk &lt;Asterisk Version&gt;">
					<synopsis>Value used in User-Agent header for SIP requests and Server header for SIP responses.</synopsis>
				</configOption>
				<configOption name="regcontext" default="">
					<synopsis>When set, Asterisk will dynamically create and destroy a NoOp priority 1 extension for a given
						peer who registers or unregisters with us.</synopsis>
				</configOption>
				<configOption name="default_outbound_endpoint" default="default_outbound_endpoint">
					<synopsis>Endpoint to use when sending an outbound request to a URI without a specified endpoint.</synopsis>
				</configOption>
				<configOption name="default_voicemail_extension">
					<synopsis>The voicemail extension to send in the NOTIFY Message-Account header if not specified on endpoint or aor</synopsis>
				</configOption>
				<configOption name="debug" default="no">
					<synopsis>Enable/Disable SIP debug logging.  Valid options include yes, no, or
						a host address</synopsis>
				</configOption>
				<configOption name="endpoint_identifier_order">
					<synopsis>The order by which endpoint identifiers are processed and checked.
						Identifier names are usually derived from and can be found in the endpoint
						identifier module itself (res_pjsip_endpoint_identifier_*).
						You can use the CLI command "pjsip show identifiers" to see the
						identifiers currently available.</synopsis>
					<description>
						<note><para>
						One of the identifiers is "auth_username" which matches on the username in
						an Authentication header.  This method has some security considerations because an
						Authentication header is not present on the first message of a dialog when
						digest authentication is used.  The client can't generate it until the server
						sends the challenge in a 401 response.  Since Asterisk normally sends a security
						event when an incoming request can't be matched to an endpoint, using auth_username
						requires that the security event be deferred until a request is received with
						the Authentication header and only generated if the username doesn't result in a
						match.  This may result in a delay before an attack is recognized.  You can control
						how many unmatched requests are received from a single ip address before a security
						event is generated using the unidentified_request parameters.
						</para></note>
					</description>
				</configOption>
				<configOption name="default_from_user" default="asterisk">
					<synopsis>When Asterisk generates an outgoing SIP request, the From header username will be
						set to this value if there is no better option (such as CallerID) to be
						used.</synopsis>
				</configOption>
				<configOption name="default_realm" default="asterisk">
					<synopsis>When Asterisk generates a challenge, the digest realm will be
						set to this value if there is no better option (such as auth/realm) to be
						used.</synopsis>
				</configOption>
				<configOption name="mwi_tps_queue_high" default="500">
					<synopsis>MWI taskprocessor high water alert trigger level.</synopsis>
					<description>
						<para>On a heavily loaded system you may need to adjust the
						taskprocessor queue limits.  If any taskprocessor queue size
						reaches its high water level then pjsip will stop processing
						new requests until the alert is cleared.  The alert clears
						when all alerting taskprocessor queues have dropped to their
						low water clear level.
						</para>
					</description>
				</configOption>
				<configOption name="mwi_tps_queue_low" default="-1">
					<synopsis>MWI taskprocessor low water clear alert level.</synopsis>
					<description>
						<para>On a heavily loaded system you may need to adjust the
						taskprocessor queue limits.  If any taskprocessor queue size
						reaches its high water level then pjsip will stop processing
						new requests until the alert is cleared.  The alert clears
						when all alerting taskprocessor queues have dropped to their
						low water clear level.
						</para>
						<note><para>Set to -1 for the low water level to be 90% of
						the high water level.</para></note>
					</description>
				</configOption>
				<configOption name="mwi_disable_initial_unsolicited" default="no">
					<synopsis>Enable/Disable sending unsolicited MWI to all endpoints on startup.</synopsis>
					<description>
						<para>When the initial unsolicited MWI notification are
						enabled on startup then the initial notifications
						get sent at startup.  If you have a lot of endpoints
						(thousands) that use unsolicited MWI then you may
						want to consider disabling the initial startup
						notifications.
						</para>
						<para>When the initial unsolicited MWI notifications are
						disabled on startup then the notifications will start
						on the endpoint's next contact update.
						</para>
					</description>
				</configOption>
				<configOption name="ignore_uri_user_options">
					<synopsis>Enable/Disable ignoring SIP URI user field options.</synopsis>
					<description>
						<para>If you have this option enabled and there are semicolons
						in the user field of a SIP URI then the field is truncated
						at the first semicolon.  This effectively makes the semicolon
						a non-usable character for PJSIP endpoint names, extensions,
						and AORs.  This can be useful for improving compatibility with
						an ITSP that likes to use user options for whatever reason.
						</para>
						<example title="Sample SIP URI">
							sip:1235557890;phone-context=national@x.x.x.x;user=phone
						</example>
						<example title="Sample SIP URI user field">
							1235557890;phone-context=national
						</example>
						<example title="Sample SIP URI user field truncated">
							1235557890
						</example>
						<note><para>The caller-id and redirecting number strings
						obtained from incoming SIP URI user fields are always truncated
						at the first semicolon.</para></note>
					</description>
				</configOption>
				<configOption name="use_callerid_contact" default="no">
					<synopsis>Place caller-id information into Contact header</synopsis>
					<description><para>
						This option will cause Asterisk to place caller-id information into
						generated Contact headers.</para>
					</description>
				</configOption>
				<configOption name="send_contact_status_on_update_registration" default="no">
					<synopsis>Enable sending AMI ContactStatus event when a device refreshes its registration.</synopsis>
				</configOption>
				<configOption name="taskprocessor_overload_trigger">
					<synopsis>Trigger scope for taskprocessor overloads</synopsis>
					<description><para>
						This option specifies the trigger the distributor will use for
						detecting taskprocessor overloads.  When it detects an overload condition,
						the distrubutor will stop accepting new requests until the overload is
						cleared.
						</para>
						<enumlist>
							<enum name="global"><para>(default) Any taskprocessor overload will trigger.</para></enum>
							<enum name="pjsip_only"><para>Only pjsip taskprocessor overloads will trigger.</para></enum>
							<enum name="none"><para>No overload detection will be performed.</para></enum>
						</enumlist>
						<warning><para>
						The "none" and "pjsip_only" options should be used
						with extreme caution and only to mitigate specific issues.
						Under certain conditions they could make things worse.
						</para></warning>
					</description>
				</configOption>
				<configOption name="norefersub" default="yes">
					<synopsis>Advertise support for RFC4488 REFER subscription suppression</synopsis>
				</configOption>
			</configObject>
		</configFile>
	</configInfo>
	<manager name="PJSIPQualify" language="en_US">
		<synopsis>
			Qualify a chan_pjsip endpoint.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Endpoint" required="true">
				<para>The endpoint you want to qualify.</para>
			</parameter>
		</syntax>
		<description>
			<para>Qualify a chan_pjsip endpoint.</para>
		</description>
	</manager>
	<managerEvent language="en_US" name="IdentifyDetail">
		<managerEventInstance class="EVENT_FLAG_COMMAND">
			<synopsis>Provide details about an identify section.</synopsis>
			<syntax>
				<parameter name="ObjectType">
					<para>The object's type. This will always be 'identify'.</para>
				</parameter>
				<parameter name="ObjectName">
					<para>The name of this object.</para>
				</parameter>
				<parameter name="Endpoint">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip_endpoint_identifier_ip']/configFile[@name='pjsip.conf']/configObject[@name='identify']/configOption[@name='endpoint']/synopsis/node())"/></para>
				</parameter>
				<parameter name="SrvLookups">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip_endpoint_identifier_ip']/configFile[@name='pjsip.conf']/configObject[@name='identify']/configOption[@name='srv_lookups']/synopsis/node())"/></para>
				</parameter>
				<parameter name="Match">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip_endpoint_identifier_ip']/configFile[@name='pjsip.conf']/configObject[@name='identify']/configOption[@name='match']/synopsis/node())"/></para>
				</parameter>
				<parameter name="MatchHeader">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip_endpoint_identifier_ip']/configFile[@name='pjsip.conf']/configObject[@name='identify']/configOption[@name='match_header']/synopsis/node())"/></para>
				</parameter>
				<parameter name="EndpointName">
					<para>The name of the endpoint associated with this information.</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="AorDetail">
		<managerEventInstance class="EVENT_FLAG_COMMAND">
			<synopsis>Provide details about an Address of Record (AoR) section.</synopsis>
			<syntax>
				<parameter name="ObjectType">
					<para>The object's type. This will always be 'aor'.</para>
				</parameter>
				<parameter name="ObjectName">
					<para>The name of this object.</para>
				</parameter>
				<parameter name="MinimumExpiration">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='aor']/configOption[@name='minimum_expiration']/synopsis/node())"/></para>
				</parameter>
				<parameter name="MaximumExpiration">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='aor']/configOption[@name='maximum_expiration']/synopsis/node())"/></para>
				</parameter>
				<parameter name="DefaultExpiration">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='aor']/configOption[@name='default_expiration']/synopsis/node())"/></para>
				</parameter>
				<parameter name="QualifyFrequency">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='aor']/configOption[@name='qualify_frequency']/synopsis/node())"/></para>
				</parameter>
				<parameter name="AuthenticateQualify">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='aor']/configOption[@name='authenticate_qualify']/synopsis/node())"/></para>
				</parameter>
				<parameter name="MaxContacts">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='aor']/configOption[@name='max_contacts']/synopsis/node())"/></para>
				</parameter>
				<parameter name="RemoveExisting">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='aor']/configOption[@name='remove_existing']/synopsis/node())"/></para>
				</parameter>
				<parameter name="RemoveUnavailable">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='aor']/configOption[@name='remove_unavailable']/synopsis/node())"/></para>
				</parameter>
				<parameter name="Mailboxes">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='aor']/configOption[@name='mailboxes']/synopsis/node())"/></para>
				</parameter>
				<parameter name="OutboundProxy">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='aor']/configOption[@name='outbound_proxy']/synopsis/node())"/></para>
				</parameter>
				<parameter name="SupportPath">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='aor']/configOption[@name='support_path']/synopsis/node())"/></para>
				</parameter>
				<parameter name="TotalContacts">
					<para>The total number of contacts associated with this AoR.</para>
				</parameter>
				<parameter name="ContactsRegistered">
					<para>The number of non-permanent contacts associated with this AoR.</para>
				</parameter>
				<parameter name="EndpointName">
					<para>The name of the endpoint associated with this information.</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="AuthDetail">
		<managerEventInstance class="EVENT_FLAG_COMMAND">
			<synopsis>Provide details about an authentication section.</synopsis>
			<syntax>
				<parameter name="ObjectType">
					<para>The object's type. This will always be 'auth'.</para>
				</parameter>
				<parameter name="ObjectName">
					<para>The name of this object.</para>
				</parameter>
				<parameter name="Username">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='auth']/configOption[@name='username']/synopsis/node())"/></para>
				</parameter>
				<parameter name="Password">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='auth']/configOption[@name='username']/synopsis/node())"/></para>
				</parameter>
				<parameter name="Md5Cred">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='auth']/configOption[@name='md5_cred']/synopsis/node())"/></para>
				</parameter>
				<parameter name="Realm">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='auth']/configOption[@name='realm']/synopsis/node())"/></para>
				</parameter>
				<parameter name="NonceLifetime">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='auth']/configOption[@name='nonce_lifetime']/synopsis/node())"/></para>
				</parameter>
				<parameter name="AuthType">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='auth']/configOption[@name='auth_type']/synopsis/node())"/></para>
				</parameter>
				<parameter name="EndpointName">
					<para>The name of the endpoint associated with this information.</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="TransportDetail">
		<managerEventInstance class="EVENT_FLAG_COMMAND">
			<synopsis>Provide details about an authentication section.</synopsis>
			<syntax>
				<parameter name="ObjectType">
					<para>The object's type. This will always be 'transport'.</para>
				</parameter>
				<parameter name="ObjectName">
					<para>The name of this object.</para>
				</parameter>
				<parameter name="Protocol">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='transport']/configOption[@name='protocol']/synopsis/node())"/></para>
				</parameter>
				<parameter name="Bind">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='transport']/configOption[@name='bind']/synopsis/node())"/></para>
				</parameter>
				<parameter name="AsycOperations">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='transport']/configOption[@name='async_operations']/synopsis/node())"/></para>
				</parameter>
				<parameter name="CaListFile">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='transport']/configOption[@name='ca_list_file']/synopsis/node())"/></para>
				</parameter>
				<parameter name="CaListPath">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='transport']/configOption[@name='ca_list_path']/synopsis/node())"/></para>
				</parameter>
				<parameter name="CertFile">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='transport']/configOption[@name='cert_file']/synopsis/node())"/></para>
				</parameter>
				<parameter name="PrivKeyFile">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='transport']/configOption[@name='priv_key_file']/synopsis/node())"/></para>
				</parameter>
				<parameter name="Password">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='transport']/configOption[@name='password']/synopsis/node())"/></para>
				</parameter>
				<parameter name="ExternalSignalingAddress">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='transport']/configOption[@name='external_signaling_address']/synopsis/node())"/></para>
				</parameter>
				<parameter name="ExternalSignalingPort">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='transport']/configOption[@name='external_signaling_port']/synopsis/node())"/></para>
				</parameter>
				<parameter name="ExternalMediaAddress">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='transport']/configOption[@name='external_media_address']/synopsis/node())"/></para>
				</parameter>
				<parameter name="Domain">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='transport']/configOption[@name='domain']/synopsis/node())"/></para>
				</parameter>
				<parameter name="VerifyServer">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='transport']/configOption[@name='verify_server']/synopsis/node())"/></para>
				</parameter>
				<parameter name="VerifyClient">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='transport']/configOption[@name='verify_client']/synopsis/node())"/></para>
				</parameter>
				<parameter name="RequireClientCert">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='transport']/configOption[@name='require_client_cert']/synopsis/node())"/></para>
				</parameter>
				<parameter name="Method">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='transport']/configOption[@name='method']/synopsis/node())"/></para>
				</parameter>
				<parameter name="Cipher">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='transport']/configOption[@name='cipher']/synopsis/node())"/></para>
				</parameter>
				<parameter name="LocalNet">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='transport']/configOption[@name='local_net']/synopsis/node())"/></para>
				</parameter>
				<parameter name="Tos">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='transport']/configOption[@name='tos']/synopsis/node())"/></para>
				</parameter>
				<parameter name="Cos">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='transport']/configOption[@name='cos']/synopsis/node())"/></para>
				</parameter>
				<parameter name="WebsocketWriteTimeout">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='transport']/configOption[@name='websocket_write_timeout']/synopsis/node())"/></para>
				</parameter>
				<parameter name="EndpointName">
					<para>The name of the endpoint associated with this information.</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="EndpointDetail">
		<managerEventInstance class="EVENT_FLAG_COMMAND">
			<synopsis>Provide details about an endpoint section.</synopsis>
			<syntax>
				<parameter name="ObjectType">
					<para>The object's type. This will always be 'endpoint'.</para>
				</parameter>
				<parameter name="ObjectName">
					<para>The name of this object.</para>
				</parameter>
				<parameter name="Context">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='context']/synopsis/node())"/></para>
				</parameter>
				<parameter name="Disallow">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='disallow']/synopsis/node())"/></para>
				</parameter>
				<parameter name="Allow">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='allow']/synopsis/node())"/></para>
				</parameter>
				<parameter name="DtmfMode">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='dtmf_mode']/synopsis/node())"/></para>
				</parameter>
				<parameter name="RtpIpv6">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='rtp_ipv6']/synopsis/node())"/></para>
				</parameter>
				<parameter name="RtpSymmetric">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='rtp_symmetric']/synopsis/node())"/></para>
				</parameter>
				<parameter name="IceSupport">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='ice_support']/synopsis/node())"/></para>
				</parameter>
				<parameter name="UsePtime">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='use_ptime']/synopsis/node())"/></para>
				</parameter>
				<parameter name="ForceRport">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='force_rport']/synopsis/node())"/></para>
				</parameter>
				<parameter name="RewriteContact">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='rewrite_contact']/synopsis/node())"/></para>
				</parameter>
				<parameter name="Transport">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='transport']/synopsis/node())"/></para>
				</parameter>
				<parameter name="OutboundProxy">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='outbound_proxy']/synopsis/node())"/></para>
				</parameter>
				<parameter name="MohSuggest">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='moh_suggest']/synopsis/node())"/></para>
				</parameter>
				<parameter name="100rel">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='100rel']/synopsis/node())"/></para>
				</parameter>
				<parameter name="Timers">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='timers']/synopsis/node())"/></para>
				</parameter>
				<parameter name="TimersMinSe">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='timers_min_se']/synopsis/node())"/></para>
				</parameter>
				<parameter name="TimersSessExpires">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='timers_sess_expires']/synopsis/node())"/></para>
				</parameter>
				<parameter name="Auth">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='auth']/synopsis/node())"/></para>
				</parameter>
				<parameter name="OutboundAuth">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='outbound_auth']/synopsis/node())"/></para>
				</parameter>
				<parameter name="Aors">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='aors']/synopsis/node())"/></para>
				</parameter>
				<parameter name="MediaAddress">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='media_address']/synopsis/node())"/></para>
				</parameter>
				<parameter name="IdentifyBy">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='identify_by']/synopsis/node())"/></para>
				</parameter>
				<parameter name="DirectMedia">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='direct_media']/synopsis/node())"/></para>
				</parameter>
				<parameter name="DirectMediaMethod">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='direct_media_method']/synopsis/node())"/></para>
				</parameter>
				<parameter name="TrustConnectedLine">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='trust_connected_line']/synopsis/node())"/></para>
				</parameter>
				<parameter name="SendConnectedLine">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='send_connected_line']/synopsis/node())"/></para>
				</parameter>
				<parameter name="ConnectedLineMethod">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='connected_line_method']/synopsis/node())"/></para>
				</parameter>
				<parameter name="DirectMediaGlareMitigation">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='direct_media_glare_mitigation']/synopsis/node())"/></para>
				</parameter>
				<parameter name="DisableDirectMediaOnNat">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='disable_direct_media_on_nat']/synopsis/node())"/></para>
				</parameter>
				<parameter name="Callerid">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='callerid']/synopsis/node())"/></para>
				</parameter>
				<parameter name="CalleridPrivacy">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='callerid_privacy']/synopsis/node())"/></para>
				</parameter>
				<parameter name="CalleridTag">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='callerid_tag']/synopsis/node())"/></para>
				</parameter>
				<parameter name="TrustIdInbound">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='trust_id_inbound']/synopsis/node())"/></para>
				</parameter>
				<parameter name="TrustIdOutbound">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='trust_id_outbound']/synopsis/node())"/></para>
				</parameter>
				<parameter name="SendPai">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='send_pai']/synopsis/node())"/></para>
				</parameter>
				<parameter name="SendRpid">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='send_rpid']/synopsis/node())"/></para>
				</parameter>
				<parameter name="SendDiversion">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='send_diversion']/synopsis/node())"/></para>
				</parameter>
				<parameter name="Mailboxes">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='mailboxes']/synopsis/node())"/></para>
				</parameter>
				<parameter name="AggregateMwi">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='aggregate_mwi']/synopsis/node())"/></para>
				</parameter>
				<parameter name="MediaEncryption">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='media_encryption']/synopsis/node())"/></para>
				</parameter>
				<parameter name="MediaEncryptionOptimistic">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='media_encryption_optimistic']/synopsis/node())"/></para>
				</parameter>
				<parameter name="UseAvpf">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='use_avpf']/synopsis/node())"/></para>
				</parameter>
				<parameter name="ForceAvp">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='force_avp']/synopsis/node())"/></para>
				</parameter>
				<parameter name="MediaUseReceivedTransport">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='media_use_received_transport']/synopsis/node())"/></para>
				</parameter>
				<parameter name="OneTouchRecording">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='one_touch_recording']/synopsis/node())"/></para>
				</parameter>
				<parameter name="InbandProgress">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='inband_progress']/synopsis/node())"/></para>
				</parameter>
				<parameter name="CallGroup">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='call_group']/synopsis/node())"/></para>
				</parameter>
				<parameter name="PickupGroup">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='pickup_group']/synopsis/node())"/></para>
				</parameter>
				<parameter name="NamedCallGroup">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='named_call_group']/synopsis/node())"/></para>
				</parameter>
				<parameter name="NamedPickupGroup">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='named_pickup_group']/synopsis/node())"/></para>
				</parameter>
				<parameter name="DeviceStateBusyAt">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='device_state_busy_at']/synopsis/node())"/></para>
				</parameter>
				<parameter name="T38Udptl">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='t38_udptl']/synopsis/node())"/></para>
				</parameter>
				<parameter name="T38UdptlEc">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='t38_udptl_ec']/synopsis/node())"/></para>
				</parameter>
				<parameter name="T38UdptlMaxdatagram">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='t38_udptl_maxdatagram']/synopsis/node())"/></para>
				</parameter>
				<parameter name="FaxDetect">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='fax_detect']/synopsis/node())"/></para>
				</parameter>
				<parameter name="T38UdptlNat">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='t38_udptl_nat']/synopsis/node())"/></para>
				</parameter>
				<parameter name="T38UdptlIpv6">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='t38_udptl_ipv6']/synopsis/node())"/></para>
				</parameter>
				<parameter name="T38BindUdptlToMediaAddress">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='t38_bind_udptl_to_media_address']/synopsis/node())"/></para>
				</parameter>
				<parameter name="ToneZone">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='tone_zone']/synopsis/node())"/></para>
				</parameter>
				<parameter name="Language">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='language']/synopsis/node())"/></para>
				</parameter>
				<parameter name="RecordOnFeature">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='record_on_feature']/synopsis/node())"/></para>
				</parameter>
				<parameter name="RecordOffFeature">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='record_off_feature']/synopsis/node())"/></para>
				</parameter>
				<parameter name="AllowTransfer">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='allow_transfer']/synopsis/node())"/></para>
				</parameter>
				<parameter name="UserEqPhone">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='user_eq_phone']/synopsis/node())"/></para>
				</parameter>
				<parameter name="MohPassthrough">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='moh_passthrough']/synopsis/node())"/></para>
				</parameter>
				<parameter name="SdpOwner">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='sdp_owner']/synopsis/node())"/></para>
				</parameter>
				<parameter name="SdpSession">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='sdp_session']/synopsis/node())"/></para>
				</parameter>
				<parameter name="TosAudio">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='tos_audio']/synopsis/node())"/></para>
				</parameter>
				<parameter name="TosVideo">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='tos_video']/synopsis/node())"/></para>
				</parameter>
				<parameter name="CosAudio">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='cos_audio']/synopsis/node())"/></para>
				</parameter>
				<parameter name="CosVideo">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='cos_video']/synopsis/node())"/></para>
				</parameter>
				<parameter name="AllowSubscribe">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='allow_subscribe']/synopsis/node())"/></para>
				</parameter>
				<parameter name="SubMinExpiry">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='sub_min_expiry']/synopsis/node())"/></para>
				</parameter>
				<parameter name="FromUser">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='from_user']/synopsis/node())"/></para>
				</parameter>
				<parameter name="FromDomain">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='from_domain']/synopsis/node())"/></para>
				</parameter>
				<parameter name="MwiFromUser">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='mwi_from_user']/synopsis/node())"/></para>
				</parameter>
				<parameter name="RtpEngine">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='rtp_engine']/synopsis/node())"/></para>
				</parameter>
				<parameter name="DtlsVerify">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='dtls_verify']/synopsis/node())"/></para>
				</parameter>
				<parameter name="DtlsRekey">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='dtls_rekey']/synopsis/node())"/></para>
				</parameter>
				<parameter name="DtlsCertFile">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='dtls_cert_file']/synopsis/node())"/></para>
				</parameter>
				<parameter name="DtlsPrivateKey">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='dtls_private_key']/synopsis/node())"/></para>
				</parameter>
				<parameter name="DtlsCipher">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='dtls_cipher']/synopsis/node())"/></para>
				</parameter>
				<parameter name="DtlsCaFile">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='dtls_ca_file']/synopsis/node())"/></para>
				</parameter>
				<parameter name="DtlsCaPath">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='dtls_ca_path']/synopsis/node())"/></para>
				</parameter>
				<parameter name="DtlsSetup">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='dtls_setup']/synopsis/node())"/></para>
				</parameter>
				<parameter name="SrtpTag32">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='srtp_tag_32']/synopsis/node())"/></para>
				</parameter>
				<parameter name="RedirectMethod">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='redirect_method']/synopsis/node())"/></para>
				</parameter>
				<parameter name="SetVar">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='set_var']/synopsis/node())"/></para>
				</parameter>
				<parameter name="MessageContext">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='message_context']/synopsis/node())"/></para>
				</parameter>
				<parameter name="Accountcode">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='accountcode']/synopsis/node())"/></para>
				</parameter>
				<parameter name="PreferredCodecOnly">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='preferred_codec_only']/synopsis/node())"/></para>
				</parameter>
				<parameter name="DeviceState">
					<para>The aggregate device state for this endpoint.</para>
				</parameter>
				<parameter name="ActiveChannels">
					<para>The number of active channels associated with this endpoint.</para>
				</parameter>
				<parameter name="SubscribeContext">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='subscribe_context']/synopsis/node())"/></para>
				</parameter>
				<parameter name="Allowoverlap">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='endpoint']/configOption[@name='allow_overlap']/synopsis/node())"/></para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="AorList">
		<managerEventInstance class="EVENT_FLAG_COMMAND">
			<synopsis>Provide details about an Address of Record (AoR) section.</synopsis>
			<syntax>
				<parameter name="ObjectType">
					<para>The object's type. This will always be 'aor'.</para>
				</parameter>
				<parameter name="ObjectName">
					<para>The name of this object.</para>
				</parameter>
				<parameter name="MinimumExpiration">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='aor']/configOption[@name='minimum_expiration']/synopsis/node())"/></para>
				</parameter>
				<parameter name="MaximumExpiration">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='aor']/configOption[@name='maximum_expiration']/synopsis/node())"/></para>
				</parameter>
				<parameter name="DefaultExpiration">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='aor']/configOption[@name='default_expiration']/synopsis/node())"/></para>
				</parameter>
				<parameter name="QualifyFrequency">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='aor']/configOption[@name='qualify_frequency']/synopsis/node())"/></para>
				</parameter>
				<parameter name="AuthenticateQualify">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='aor']/configOption[@name='authenticate_qualify']/synopsis/node())"/></para>
				</parameter>
				<parameter name="MaxContacts">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='aor']/configOption[@name='max_contacts']/synopsis/node())"/></para>
				</parameter>
				<parameter name="RemoveExisting">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='aor']/configOption[@name='remove_existing']/synopsis/node())"/></para>
				</parameter>
				<parameter name="RemoveUnavailable">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='aor']/configOption[@name='remove_unavailable']/synopsis/node())"/></para>
				</parameter>
				<parameter name="Mailboxes">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='aor']/configOption[@name='mailboxes']/synopsis/node())"/></para>
				</parameter>
				<parameter name="OutboundProxy">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='aor']/configOption[@name='outbound_proxy']/synopsis/node())"/></para>
				</parameter>
				<parameter name="SupportPath">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='aor']/configOption[@name='support_path']/synopsis/node())"/></para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="AuthList">
		<managerEventInstance class="EVENT_FLAG_COMMAND">
			<synopsis>Provide details about an Address of Record (Auth) section.</synopsis>
			<syntax>
				<parameter name="ObjectType">
					<para>The object's type. This will always be 'auth'.</para>
				</parameter>
				<parameter name="ObjectName">
					<para>The name of this object.</para>
				</parameter>
				<parameter name="Username">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='auth']/configOption[@name='username']/synopsis/node())"/></para>
				</parameter>
				<parameter name="Md5Cred">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='auth']/configOption[@name='md5_cred']/synopsis/node())"/></para>
				</parameter>
				<parameter name="Realm">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='auth']/configOption[@name='realm']/synopsis/node())"/></para>
				</parameter>
				<parameter name="AuthType">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='auth']/configOption[@name='auth_type']/synopsis/node())"/></para>
				</parameter>
				<parameter name="Password">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='auth']/configOption[@name='password']/synopsis/node())"/></para>
				</parameter>
				<parameter name="NonceLifetime">
					<para><xi:include xpointer="xpointer(/docs/configInfo[@name='res_pjsip']/configFile[@name='pjsip.conf']/configObject[@name='auth']/configOption[@name='nonce_lifetime']/synopsis/node())"/></para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ContactList">
		<managerEventInstance class="EVENT_FLAG_COMMAND">
			<synopsis>Provide details about a contact section.</synopsis>
			<syntax>
				<parameter name="ObjectType">
					<para>The object's type. This will always be 'contact'.</para>
				</parameter>
				<parameter name="ObjectName">
					<para>The name of this object.</para>
				</parameter>
				<parameter name="ViaAddr">
					<para>IP address of the last Via header in REGISTER request.
					Will only appear in the event if available.</para>
				</parameter>
				<parameter name="ViaPort">
					<para>Port number of the last Via header in REGISTER request.
					Will only appear in the event if available.</para>
				</parameter>
				<parameter name="QualifyTimeout">
					<para>The elapsed time in decimal seconds after which an OPTIONS
					message is sent before the contact is considered unavailable.</para>
				</parameter>
				<parameter name="CallId">
					<para>Content of the Call-ID header in REGISTER request.
					Will only appear in the event if available.</para>
				</parameter>
				<parameter name="RegServer">
					<para>Asterisk Server name.</para>
				</parameter>
				<parameter name="PruneOnBoot">
					<para>If true delete the contact on Asterisk restart/boot.</para>
				</parameter>
				<parameter name="Path">
					<para>The Path header received on the REGISTER.</para>
				</parameter>
				<parameter name="Endpoint">
					<para>The name of the endpoint associated with this information.</para>
				</parameter>
				<parameter name="AuthenticateQualify">
					<para>A boolean indicating whether a qualify should be authenticated.</para>
				</parameter>
				<parameter name="Uri">
					<para>This contact's URI.</para>
				</parameter>
				<parameter name="QualifyFrequency">
					<para>The interval in seconds at which the contact will be qualified.</para>
				</parameter>
				<parameter name="UserAgent">
					<para>Content of the User-Agent header in REGISTER request</para>
				</parameter>
				<parameter name="ExpirationTime">
					<para>Absolute time that this contact is no longer valid after</para>
				</parameter>
				<parameter name="OutboundProxy">
					<para>The contact's outbound proxy.</para>
				</parameter>
				<parameter name="Status">
					<para>This contact's status.</para>
					<enumlist>
						<enum name="Reachable"/>
						<enum name="Unreachable"/>
						<enum name="NonQualified"/>
						<enum name="Unknown"/>
					</enumlist>
				</parameter>
				<parameter name="RoundtripUsec">
					<para>The round trip time in microseconds.</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="ContactStatusDetail">
		<managerEventInstance class="EVENT_FLAG_COMMAND">
			<synopsis>Provide details about a contact's status.</synopsis>
			<syntax>
				<parameter name="AOR">
					<para>The AoR that owns this contact.</para>
				</parameter>
				<parameter name="URI">
					<para>This contact's URI.</para>
				</parameter>
				<parameter name="Status">
					<para>This contact's status.</para>
					<enumlist>
						<enum name="Reachable"/>
						<enum name="Unreachable"/>
						<enum name="NonQualified"/>
						<enum name="Unknown"/>
					</enumlist>
				</parameter>
				<parameter name="RoundtripUsec">
					<para>The round trip time in microseconds.</para>
				</parameter>
				<parameter name="EndpointName">
					<para>The name of the endpoint associated with this information.</para>
				</parameter>
				<parameter name="UserAgent">
					<para>Content of the User-Agent header in REGISTER request</para>
				</parameter>
				<parameter name="RegExpire">
					<para>Absolute time that this contact is no longer valid after</para>
				</parameter>
				<parameter name="ViaAddress">
					<para>IP address:port of the last Via header in REGISTER request.
					Will only appear in the event if available.</para>
				</parameter>
				<parameter name="CallID">
					<para>Content of the Call-ID header in REGISTER request.
					Will only appear in the event if available.</para>
				</parameter>
				<parameter name="ID">
					<para>The sorcery ID of the contact.</para>
				</parameter>
				<parameter name="AuthenticateQualify">
					<para>A boolean indicating whether a qualify should be authenticated.</para>
				</parameter>
				<parameter name="OutboundProxy">
					<para>The contact's outbound proxy.</para>
				</parameter>
				<parameter name="Path">
					<para>The Path header received on the REGISTER.</para>
				</parameter>
				<parameter name="QualifyFrequency">
					<para>The interval in seconds at which the contact will be qualified.</para>
				</parameter>
				<parameter name="QualifyTimeout">
					<para>The elapsed time in decimal seconds after which an OPTIONS
					message is sent before the contact is considered unavailable.</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<managerEvent language="en_US" name="EndpointList">
		<managerEventInstance class="EVENT_FLAG_COMMAND">
			<synopsis>Provide details about a contact's status.</synopsis>
			<syntax>
				<parameter name="ObjectType">
					<para>The object's type. This will always be 'endpoint'.</para>
				</parameter>
				<parameter name="ObjectName">
					<para>The name of this object.</para>
				</parameter>
				<parameter name="Transport">
					<para>The transport configurations associated with this endpoint.</para>
				</parameter>
				<parameter name="Aor">
					<para>The aor configurations associated with this endpoint.</para>
				</parameter>
				<parameter name="Auths">
					<para>The inbound authentication configurations associated with this endpoint.</para>
				</parameter>
				<parameter name="OutboundAuths">
					<para>The outbound authentication configurations associated with this endpoint.</para>
				</parameter>
				<parameter name="DeviceState">
					<para>The aggregate device state for this endpoint.</para>
				</parameter>
				<parameter name="ActiveChannels">
					<para>The number of active channels associated with this endpoint.</para>
				</parameter>
			</syntax>
		</managerEventInstance>
	</managerEvent>
	<manager name="PJSIPShowEndpoints" language="en_US">
		<synopsis>
			Lists PJSIP endpoints.
		</synopsis>
		<syntax />
		<description>
			<para>
			Provides a listing of all endpoints.  For each endpoint an <literal>EndpointList</literal> event
			is raised that contains relevant attributes and status information.  Once all
			endpoints have been listed an <literal>EndpointListComplete</literal> event is issued.
			</para>
		</description>
		<responses>
			<list-elements>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='EndpointList'])" />
			</list-elements>
			<managerEvent language="en_US" name="EndpointListComplete">
				<managerEventInstance class="EVENT_FLAG_COMMAND">
					<synopsis>Provide final information about an endpoint list.</synopsis>
					<syntax>
						<parameter name="EventList"/>
						<parameter name="ListItems"/>
					</syntax>
				</managerEventInstance>
			</managerEvent>
		</responses>
	</manager>
	<manager name="PJSIPShowEndpoint" language="en_US">
		<synopsis>
			Detail listing of an endpoint and its objects.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Endpoint" required="true">
				<para>The endpoint to list.</para>
			</parameter>
		</syntax>
		<description>
			<para>
			Provides a detailed listing of options for a given endpoint.  Events are issued
			showing the configuration and status of the endpoint and associated objects.  These
			events include <literal>EndpointDetail</literal>, <literal>AorDetail</literal>,
			<literal>AuthDetail</literal>, <literal>TransportDetail</literal>, and
			<literal>IdentifyDetail</literal>.  Some events may be listed multiple times if multiple objects are
			associated (for instance AoRs).  Once all detail events have been raised a final
			<literal>EndpointDetailComplete</literal> event is issued.
			</para>
		</description>
		<responses>
			<list-elements>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='EndpointDetail'])" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='IdentifyDetail'])" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='ContactStatusDetail'])" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='AuthDetail'])" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='TransportDetail'])" />
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='AorDetail'])" />
			</list-elements>
			<managerEvent language="en_US" name="EndpointDetailComplete">
				<managerEventInstance class="EVENT_FLAG_COMMAND">
					<synopsis>Provide final information about endpoint details.</synopsis>
					<syntax>
						<parameter name="EventList"/>
						<parameter name="ListItems"/>
					</syntax>
				</managerEventInstance>
			</managerEvent>
		</responses>
	</manager>
	<manager name="PJSIPShowAors" language="en_US">
		<synopsis>
			Lists PJSIP AORs.
		</synopsis>
		<syntax />
		<description>
			<para>
			Provides a listing of all AORs. For each AOR an <literal>AorList</literal> event
			is raised that contains relevant attributes and status information.  Once all
			aors have been listed an <literal>AorListComplete</literal> event is issued.
			</para>
		</description>
		<responses>
			<list-elements>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='AorList'])" />
			</list-elements>
			<managerEvent language="en_US" name="AorListComplete">
				<managerEventInstance class="EVENT_FLAG_COMMAND">
					<synopsis>Provide final information about an aor list.</synopsis>
					<syntax>
						<parameter name="EventList"/>
						<parameter name="ListItems"/>
					</syntax>
				</managerEventInstance>
			</managerEvent>
		</responses>
	</manager>
	<manager name="PJSIPShowAuths" language="en_US">
		<synopsis>
			Lists PJSIP Auths.
		</synopsis>
		<syntax />
		<description>
			<para>Provides a listing of all Auths. For each Auth an <literal>AuthList</literal> event
			is raised that contains relevant attributes and status information.  Once all
			auths have been listed an <literal>AuthListComplete</literal> event is issued.
			</para>
		</description>
		<responses>
			<list-elements>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='AuthList'])" />
			</list-elements>
			<managerEvent language="en_US" name="AuthListComplete">
				<managerEventInstance class="EVENT_FLAG_COMMAND">
					<synopsis>Provide final information about an auth list.</synopsis>
					<syntax>
						<parameter name="EventList"/>
						<parameter name="ListItems"/>
					</syntax>
				</managerEventInstance>
			</managerEvent>
		</responses>
	</manager>
	<manager name="PJSIPShowContacts" language="en_US">
		<synopsis>
			Lists PJSIP Contacts.
		</synopsis>
		<syntax />
		<description>
			<para>Provides a listing of all Contacts. For each Contact a <literal>ContactList</literal>
			event is raised that contains relevant attributes and status information.
			Once all contacts have been listed a <literal>ContactListComplete</literal> event
			is issued.
			</para>
		</description>
		<responses>
			<list-elements>
				<xi:include xpointer="xpointer(/docs/managerEvent[@name='ContactList'])" />
			</list-elements>
			<managerEvent language="en_US" name="ContactListComplete">
				<managerEventInstance class="EVENT_FLAG_COMMAND">
					<synopsis>Provide final information about a contact list.</synopsis>
					<syntax>
						<parameter name="EventList"/>
						<parameter name="ListItems"/>
					</syntax>
				</managerEventInstance>
			</managerEvent>
		</responses>
	</manager>

 ***/

#define MOD_DATA_CONTACT "contact"

/*! Number of serializers in pool if one not supplied. */
#define SERIALIZER_POOL_SIZE		8

/*! Pool of serializers to use if not supplied. */
static struct ast_serializer_pool *sip_serializer_pool;

static pjsip_endpoint *ast_pjsip_endpoint;

static struct ast_threadpool *sip_threadpool;

/*! Local host address for IPv4 */
static pj_sockaddr host_ip_ipv4;

/*! Local host address for IPv4 (string form) */
static char host_ip_ipv4_string[PJ_INET6_ADDRSTRLEN];

/*! Local host address for IPv6 */
static pj_sockaddr host_ip_ipv6;

/*! Local host address for IPv6 (string form) */
static char host_ip_ipv6_string[PJ_INET6_ADDRSTRLEN];

void ast_sip_add_date_header(pjsip_tx_data *tdata)
{
	char date[256];
	struct tm tm;
	time_t t = time(NULL);

	gmtime_r(&t, &tm);
	strftime(date, sizeof(date), "%a, %d %b %Y %T GMT", &tm);

	ast_sip_add_header(tdata, "Date", date);
}

static int register_service(void *data)
{
	pjsip_module **module = data;
	if (!ast_pjsip_endpoint) {
		ast_log(LOG_ERROR, "There is no PJSIP endpoint. Unable to register services\n");
		return -1;
	}
	if (pjsip_endpt_register_module(ast_pjsip_endpoint, *module) != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Unable to register module %.*s\n", (int) pj_strlen(&(*module)->name), pj_strbuf(&(*module)->name));
		return -1;
	}
	ast_debug(1, "Registered SIP service %.*s (%p)\n", (int) pj_strlen(&(*module)->name), pj_strbuf(&(*module)->name), *module);
	return 0;
}

int ast_sip_register_service(pjsip_module *module)
{
	return ast_sip_push_task_wait_servant(NULL, register_service, &module);
}

static int unregister_service(void *data)
{
	pjsip_module **module = data;
	if (!ast_pjsip_endpoint) {
		return -1;
	}
	pjsip_endpt_unregister_module(ast_pjsip_endpoint, *module);
	ast_debug(1, "Unregistered SIP service %.*s\n", (int) pj_strlen(&(*module)->name), pj_strbuf(&(*module)->name));
	return 0;
}

void ast_sip_unregister_service(pjsip_module *module)
{
	ast_sip_push_task_wait_servant(NULL, unregister_service, &module);
}

static struct ast_sip_authenticator *registered_authenticator;

int ast_sip_register_authenticator(struct ast_sip_authenticator *auth)
{
	if (registered_authenticator) {
		ast_log(LOG_WARNING, "Authenticator %p is already registered. Cannot register a new one\n", registered_authenticator);
		return -1;
	}
	registered_authenticator = auth;
	ast_debug(1, "Registered SIP authenticator module %p\n", auth);

	return 0;
}

void ast_sip_unregister_authenticator(struct ast_sip_authenticator *auth)
{
	if (registered_authenticator != auth) {
		ast_log(LOG_WARNING, "Trying to unregister authenticator %p but authenticator %p registered\n",
				auth, registered_authenticator);
		return;
	}
	registered_authenticator = NULL;
	ast_debug(1, "Unregistered SIP authenticator %p\n", auth);
}

int ast_sip_requires_authentication(struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata)
{
	if (endpoint->allow_unauthenticated_options
		&& !pjsip_method_cmp(&rdata->msg_info.msg->line.req.method, &pjsip_options_method)) {
		ast_debug(3, "Skipping OPTIONS authentication due to endpoint configuration\n");
		return 0;
	}

	if (!registered_authenticator) {
		ast_log(LOG_WARNING, "No SIP authenticator registered. Assuming authentication is not required\n");
		return 0;
	}

	return registered_authenticator->requires_authentication(endpoint, rdata);
}

enum ast_sip_check_auth_result ast_sip_check_authentication(struct ast_sip_endpoint *endpoint,
		pjsip_rx_data *rdata, pjsip_tx_data *tdata)
{
	if (!registered_authenticator) {
		ast_log(LOG_WARNING, "No SIP authenticator registered. Assuming authentication is successful\n");
		return AST_SIP_AUTHENTICATION_SUCCESS;
	}
	return registered_authenticator->check_authentication(endpoint, rdata, tdata);
}

static struct ast_sip_outbound_authenticator *registered_outbound_authenticator;

int ast_sip_register_outbound_authenticator(struct ast_sip_outbound_authenticator *auth)
{
	if (registered_outbound_authenticator) {
		ast_log(LOG_WARNING, "Outbound authenticator %p is already registered. Cannot register a new one\n", registered_outbound_authenticator);
		return -1;
	}
	registered_outbound_authenticator = auth;
	ast_debug(1, "Registered SIP outbound authenticator module %p\n", auth);

	return 0;
}

void ast_sip_unregister_outbound_authenticator(struct ast_sip_outbound_authenticator *auth)
{
	if (registered_outbound_authenticator != auth) {
		ast_log(LOG_WARNING, "Trying to unregister outbound authenticator %p but outbound authenticator %p registered\n",
				auth, registered_outbound_authenticator);
		return;
	}
	registered_outbound_authenticator = NULL;
	ast_debug(1, "Unregistered SIP outbound authenticator %p\n", auth);
}

int ast_sip_create_request_with_auth(const struct ast_sip_auth_vector *auths, pjsip_rx_data *challenge,
		pjsip_tx_data *old_request, pjsip_tx_data **new_request)
{
	if (!registered_outbound_authenticator) {
		ast_log(LOG_WARNING, "No SIP outbound authenticator registered. Cannot respond to authentication challenge\n");
		return -1;
	}
	return registered_outbound_authenticator->create_request_with_auth(auths, challenge, old_request, new_request);
}

struct endpoint_identifier_list {
	const char *name;
	unsigned int priority;
	struct ast_sip_endpoint_identifier *identifier;
	AST_RWLIST_ENTRY(endpoint_identifier_list) list;
};

static AST_RWLIST_HEAD_STATIC(endpoint_identifiers, endpoint_identifier_list);

int ast_sip_register_endpoint_identifier_with_name(struct ast_sip_endpoint_identifier *identifier,
						 const char *name)
{
	char *prev, *current, *identifier_order;
	struct endpoint_identifier_list *iter, *id_list_item;
	SCOPED_LOCK(lock, &endpoint_identifiers, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);

	id_list_item = ast_calloc(1, sizeof(*id_list_item));
	if (!id_list_item) {
		ast_log(LOG_ERROR, "Unable to add endpoint identifier. Out of memory.\n");
		return -1;
	}
	id_list_item->identifier = identifier;
	id_list_item->name = name;

	ast_debug(1, "Register endpoint identifier %s(%p)\n", name ?: "", identifier);

	if (ast_strlen_zero(name)) {
		/* if an identifier has no name then place in front */
		AST_RWLIST_INSERT_HEAD(&endpoint_identifiers, id_list_item, list);
		return 0;
	}

	/* see if the name of the identifier is in the global endpoint_identifier_order list */
	identifier_order = prev = current = ast_sip_get_endpoint_identifier_order();

	if (ast_strlen_zero(identifier_order)) {
		id_list_item->priority = UINT_MAX;
		AST_RWLIST_INSERT_TAIL(&endpoint_identifiers, id_list_item, list);
		ast_free(identifier_order);
		return 0;
	}

	id_list_item->priority = 0;
	while ((current = strchr(current, ','))) {
		++id_list_item->priority;
		if (!strncmp(prev, name, current - prev)
			&& strlen(name) == current - prev) {
			break;
		}
		prev = ++current;
	}

	if (!current) {
		/* check to see if it is the only or last item */
		if (!strcmp(prev, name)) {
			++id_list_item->priority;
		} else {
			id_list_item->priority = UINT_MAX;
		}
	}

	if (id_list_item->priority == UINT_MAX || AST_RWLIST_EMPTY(&endpoint_identifiers)) {
		/* if not in the endpoint_identifier_order list then consider it less in
		   priority and add it to the end */
		AST_RWLIST_INSERT_TAIL(&endpoint_identifiers, id_list_item, list);
		ast_free(identifier_order);
		return 0;
	}

	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&endpoint_identifiers, iter, list) {
		if (id_list_item->priority < iter->priority) {
			AST_RWLIST_INSERT_BEFORE_CURRENT(id_list_item, list);
			break;
		}

		if (!AST_RWLIST_NEXT(iter, list)) {
			AST_RWLIST_INSERT_AFTER(&endpoint_identifiers, iter, id_list_item, list);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;

	ast_free(identifier_order);
	return 0;
}

int ast_sip_register_endpoint_identifier(struct ast_sip_endpoint_identifier *identifier)
{
	return ast_sip_register_endpoint_identifier_with_name(identifier, NULL);
}

void ast_sip_unregister_endpoint_identifier(struct ast_sip_endpoint_identifier *identifier)
{
	struct endpoint_identifier_list *iter;
	SCOPED_LOCK(lock, &endpoint_identifiers, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);
	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&endpoint_identifiers, iter, list) {
		if (iter->identifier == identifier) {
			AST_RWLIST_REMOVE_CURRENT(list);
			ast_free(iter);
			ast_debug(1, "Unregistered endpoint identifier %p\n", identifier);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
}

struct ast_sip_endpoint *ast_sip_identify_endpoint(pjsip_rx_data *rdata)
{
	struct endpoint_identifier_list *iter;
	struct ast_sip_endpoint *endpoint = NULL;
	SCOPED_LOCK(lock, &endpoint_identifiers, AST_RWLIST_RDLOCK, AST_RWLIST_UNLOCK);
	AST_RWLIST_TRAVERSE(&endpoint_identifiers, iter, list) {
		ast_assert(iter->identifier->identify_endpoint != NULL);
		endpoint = iter->identifier->identify_endpoint(rdata);
		if (endpoint) {
			break;
		}
	}
	return endpoint;
}

char *ast_sip_rdata_get_header_value(pjsip_rx_data *rdata, const pj_str_t str)
{
	pjsip_generic_string_hdr *hdr;
	pj_str_t hdr_val;

	hdr = pjsip_msg_find_hdr_by_name(rdata->msg_info.msg, &str, NULL);
	if (!hdr) {
		return NULL;
	}

	pj_strdup_with_null(rdata->tp_info.pool, &hdr_val, &hdr->hvalue);

	return hdr_val.ptr;
}

static int do_cli_dump_endpt(void *v_a)
{
	struct ast_cli_args *a = v_a;

	ast_pjproject_log_intercept_begin(a->fd);
	pjsip_endpt_dump(ast_sip_get_pjsip_endpoint(), a->argc == 4 ? PJ_TRUE : PJ_FALSE);
	ast_pjproject_log_intercept_end();

	return 0;
}

static char *cli_dump_endpt(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
#ifdef AST_DEVMODE
		e->command = "pjsip dump endpt [details]";
		e->usage =
			"Usage: pjsip dump endpt [details]\n"
			"       Dump the res_pjsip endpt internals.\n"
			"\n"
			"Warning: PJPROJECT documents that the function used by this\n"
			"CLI command may cause a crash when asking for details because\n"
			"it tries to access all active memory pools.\n";
#else
		/*
		 * In non-developer mode we will not document or make easily accessible
		 * the details option even though it is still available.  The user has
		 * to know it exists to use it.  Presumably they would also be aware of
		 * the potential crash warning.
		 */
		e->command = "pjsip dump endpt";
		e->usage =
			"Usage: pjsip dump endpt\n"
			"       Dump the res_pjsip endpt internals.\n";
#endif /* AST_DEVMODE */
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (4 < a->argc
		|| (a->argc == 4 && strcasecmp(a->argv[3], "details"))) {
		return CLI_SHOWUSAGE;
	}

	ast_sip_push_task_wait_servant(NULL, do_cli_dump_endpt, a);

	return CLI_SUCCESS;
}

static char *cli_show_endpoint_identifiers(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
#define ENDPOINT_IDENTIFIER_FORMAT "%-20.20s\n"
	struct endpoint_identifier_list *iter;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pjsip show identifiers";
		e->usage = "Usage: pjsip show identifiers\n"
		            "      List all registered endpoint identifiers\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc != 3) {
                return CLI_SHOWUSAGE;
        }

	ast_cli(a->fd, ENDPOINT_IDENTIFIER_FORMAT, "Identifier Names:");
	{
		SCOPED_LOCK(lock, &endpoint_identifiers, AST_RWLIST_RDLOCK, AST_RWLIST_UNLOCK);
		AST_RWLIST_TRAVERSE(&endpoint_identifiers, iter, list) {
			ast_cli(a->fd, ENDPOINT_IDENTIFIER_FORMAT,
				iter->name ? iter->name : "name not specified");
		}
	}
	return CLI_SUCCESS;
#undef ENDPOINT_IDENTIFIER_FORMAT
}

static char *cli_show_settings(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_sip_cli_context context;

	switch (cmd) {
	case CLI_INIT:
		e->command = "pjsip show settings";
		e->usage = "Usage: pjsip show settings\n"
		            "      Show global and system configuration options\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	context.output_buffer = ast_str_create(256);
	if (!context.output_buffer) {
		ast_cli(a->fd, "Could not allocate output buffer.\n");
		return CLI_FAILURE;
	}

	if (sip_cli_print_global(&context) || sip_cli_print_system(&context)) {
		ast_free(context.output_buffer);
		ast_cli(a->fd, "Error retrieving settings.\n");
		return CLI_FAILURE;
	}

	ast_cli(a->fd, "%s", ast_str_buffer(context.output_buffer));
	ast_free(context.output_buffer);
	return CLI_SUCCESS;
}

static struct ast_cli_entry cli_commands[] = {
	AST_CLI_DEFINE(cli_dump_endpt, "Dump the res_pjsip endpt internals"),
	AST_CLI_DEFINE(cli_show_settings, "Show global and system configuration options"),
	AST_CLI_DEFINE(cli_show_endpoint_identifiers, "List registered endpoint identifiers")
};

AST_RWLIST_HEAD_STATIC(endpoint_formatters, ast_sip_endpoint_formatter);

void ast_sip_register_endpoint_formatter(struct ast_sip_endpoint_formatter *obj)
{
	SCOPED_LOCK(lock, &endpoint_formatters, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);
	AST_RWLIST_INSERT_TAIL(&endpoint_formatters, obj, next);
}

void ast_sip_unregister_endpoint_formatter(struct ast_sip_endpoint_formatter *obj)
{
	struct ast_sip_endpoint_formatter *i;
	SCOPED_LOCK(lock, &endpoint_formatters, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);

	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&endpoint_formatters, i, next) {
		if (i == obj) {
			AST_RWLIST_REMOVE_CURRENT(next);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
}

int ast_sip_format_endpoint_ami(struct ast_sip_endpoint *endpoint,
				struct ast_sip_ami *ami, int *count)
{
	int res = 0;
	struct ast_sip_endpoint_formatter *i;
	SCOPED_LOCK(lock, &endpoint_formatters, AST_RWLIST_RDLOCK, AST_RWLIST_UNLOCK);
	*count = 0;
	AST_RWLIST_TRAVERSE(&endpoint_formatters, i, next) {
		if (i->format_ami && ((res = i->format_ami(endpoint, ami)) < 0)) {
			return res;
		}

		if (!res) {
			(*count)++;
		}
	}
	return 0;
}

pjsip_endpoint *ast_sip_get_pjsip_endpoint(void)
{
	return ast_pjsip_endpoint;
}

int ast_sip_will_uri_survive_restart(pjsip_sip_uri *uri, struct ast_sip_endpoint *endpoint,
	pjsip_rx_data *rdata)
{
	pj_str_t host_name;
	int result = 1;

	/* Determine if the contact cannot survive a restart/boot. */
	if (uri->port == rdata->pkt_info.src_port
		&& !pj_strcmp(&uri->host,
			pj_cstr(&host_name, rdata->pkt_info.src_name))
		/* We have already checked if the URI scheme is sip: or sips: */
		&& PJSIP_TRANSPORT_IS_RELIABLE(rdata->tp_info.transport)) {
		pj_str_t type_name;

		/* Determine the transport parameter value */
		if (!strcasecmp("WSS", rdata->tp_info.transport->type_name)) {
			/* WSS is special, as it needs to be ws. */
			pj_cstr(&type_name, "ws");
		} else {
			pj_cstr(&type_name, rdata->tp_info.transport->type_name);
		}

		if (!pj_stricmp(&uri->transport_param, &type_name)
			&& (endpoint->nat.rewrite_contact
				/* Websockets are always rewritten */
				|| !pj_stricmp(&uri->transport_param,
					pj_cstr(&type_name, "ws")))) {
			/*
			 * The contact was rewritten to the reliable transport's
			 * source address.  Disconnecting the transport for any
			 * reason invalidates the contact.
			 */
			result = 0;
		}
	}

	return result;
}

int ast_sip_get_transport_name(const struct ast_sip_endpoint *endpoint,
	pjsip_sip_uri *sip_uri, char *buf, size_t buf_len)
{
	char *host = NULL;
	static const pj_str_t x_name = { AST_SIP_X_AST_TXP, AST_SIP_X_AST_TXP_LEN };
	pjsip_param *x_transport;

	if (!ast_strlen_zero(endpoint->transport)) {
		ast_copy_string(buf, endpoint->transport, buf_len);
		return 0;
	}

	x_transport = pjsip_param_find(&sip_uri->other_param, &x_name);
	if (!x_transport) {
		return -1;
	}

	/* Only use x_transport if the uri host is an ip (4 or 6) address */
	host = ast_alloca(sip_uri->host.slen + 1);
	ast_copy_pj_str(host, &sip_uri->host, sip_uri->host.slen + 1);
	if (!ast_sockaddr_parse(NULL, host, PARSE_PORT_FORBID)) {
		return -1;
	}

	ast_copy_pj_str(buf, &x_transport->value, buf_len);

	return 0;
}

int ast_sip_dlg_set_transport(const struct ast_sip_endpoint *endpoint, pjsip_dialog *dlg,
	pjsip_tpselector *selector)
{
	pjsip_sip_uri *uri;
	pjsip_tpselector sel = { .type = PJSIP_TPSELECTOR_NONE, };

	uri = pjsip_uri_get_uri(dlg->target);
	if (!selector) {
		selector = &sel;
	}

	ast_sip_set_tpselector_from_ep_or_uri(endpoint, uri, selector);

	pjsip_dlg_set_transport(dlg, selector);

	if (selector == &sel) {
		ast_sip_tpselector_unref(&sel);
	}

	return 0;
}

static int sip_dialog_create_from(pj_pool_t *pool, pj_str_t *from, const char *user,
	const char *domain, const pj_str_t *target, pjsip_tpselector *selector)
{
	pj_str_t tmp, local_addr;
	pjsip_uri *uri;
	pjsip_sip_uri *sip_uri;
	pjsip_transport_type_e type;
	int local_port;
	char default_user[PJSIP_MAX_URL_SIZE];

	if (ast_strlen_zero(user)) {
		ast_sip_get_default_from_user(default_user, sizeof(default_user));
		user = default_user;
	}

	/* Parse the provided target URI so we can determine what transport it will end up using */
	pj_strdup_with_null(pool, &tmp, target);

	if (!(uri = pjsip_parse_uri(pool, tmp.ptr, tmp.slen, 0)) ||
	    (!PJSIP_URI_SCHEME_IS_SIP(uri) && !PJSIP_URI_SCHEME_IS_SIPS(uri))) {
		return -1;
	}

	sip_uri = pjsip_uri_get_uri(uri);

	/* Determine the transport type to use */
	type = pjsip_transport_get_type_from_name(&sip_uri->transport_param);
	if (PJSIP_URI_SCHEME_IS_SIPS(sip_uri)) {
		if (type == PJSIP_TRANSPORT_UNSPECIFIED
			|| !(pjsip_transport_get_flag_from_type(type) & PJSIP_TRANSPORT_SECURE)) {
			type = PJSIP_TRANSPORT_TLS;
		}
	} else if (!sip_uri->transport_param.slen) {
		type = PJSIP_TRANSPORT_UDP;
	} else if (type == PJSIP_TRANSPORT_UNSPECIFIED) {
		return -1;
	}

	/* If the host is IPv6 turn the transport into an IPv6 version */
	if (pj_strchr(&sip_uri->host, ':')) {
		type |= PJSIP_TRANSPORT_IPV6;
	}

	/* In multidomain scenario, username may contain @ with domain info */
	if (!ast_sip_get_disable_multi_domain() && strchr(user, '@')) {
		from->ptr = pj_pool_alloc(pool, PJSIP_MAX_URL_SIZE);
		from->slen = pj_ansi_snprintf(from->ptr, PJSIP_MAX_URL_SIZE,
				"<sip:%s%s%s>",
				user,
				(type != PJSIP_TRANSPORT_UDP && type != PJSIP_TRANSPORT_UDP6) ? ";transport=" : "",
				(type != PJSIP_TRANSPORT_UDP && type != PJSIP_TRANSPORT_UDP6) ? pjsip_transport_get_type_name(type) : "");
		return 0;
	}

	if (!ast_strlen_zero(domain)) {
		from->ptr = pj_pool_alloc(pool, PJSIP_MAX_URL_SIZE);
		from->slen = pj_ansi_snprintf(from->ptr, PJSIP_MAX_URL_SIZE,
				"<sip:%s@%s%s%s>",
				user,
				domain,
				(type != PJSIP_TRANSPORT_UDP && type != PJSIP_TRANSPORT_UDP6) ? ";transport=" : "",
				(type != PJSIP_TRANSPORT_UDP && type != PJSIP_TRANSPORT_UDP6) ? pjsip_transport_get_type_name(type) : "");
		return 0;
	}

	/* Get the local bound address for the transport that will be used when communicating with the provided URI */
	if (pjsip_tpmgr_find_local_addr(pjsip_endpt_get_tpmgr(ast_sip_get_pjsip_endpoint()), pool, type, selector,
							      &local_addr, &local_port) != PJ_SUCCESS) {

		/* If no local address can be retrieved using the transport manager use the host one */
		pj_strdup(pool, &local_addr, pj_gethostname());
		local_port = pjsip_transport_get_default_port_for_type(PJSIP_TRANSPORT_UDP);
	}

	/* If IPv6 was specified in the transport, set the proper type */
	if (pj_strchr(&local_addr, ':')) {
		type |= PJSIP_TRANSPORT_IPV6;
	}

	from->ptr = pj_pool_alloc(pool, PJSIP_MAX_URL_SIZE);
	from->slen = pj_ansi_snprintf(from->ptr, PJSIP_MAX_URL_SIZE,
				      "<sip:%s@%s%.*s%s:%d%s%s>",
				      user,
				      (type & PJSIP_TRANSPORT_IPV6) ? "[" : "",
				      (int)local_addr.slen,
				      local_addr.ptr,
				      (type & PJSIP_TRANSPORT_IPV6) ? "]" : "",
				      local_port,
				      (type != PJSIP_TRANSPORT_UDP && type != PJSIP_TRANSPORT_UDP6) ? ";transport=" : "",
				      (type != PJSIP_TRANSPORT_UDP && type != PJSIP_TRANSPORT_UDP6) ? pjsip_transport_get_type_name(type) : "");

	return 0;
}

int ast_sip_set_tpselector_from_transport(const struct ast_sip_transport *transport, pjsip_tpselector *selector)
{
	int res = 0;
	struct ast_sip_transport_state *transport_state;

	transport_state = ast_sip_get_transport_state(ast_sorcery_object_get_id(transport));
	if (!transport_state) {
		ast_log(LOG_ERROR, "Unable to retrieve PJSIP transport state for '%s'\n",
			ast_sorcery_object_get_id(transport));
		return -1;
	}

	/* Only flows maintain dynamic state which needs protection */
	if (transport_state->flow) {
		ao2_lock(transport_state);
	}

	if (transport_state->transport) {
		selector->type = PJSIP_TPSELECTOR_TRANSPORT;
		selector->u.transport = transport_state->transport;
		pjsip_transport_add_ref(selector->u.transport);
	} else if (transport_state->factory) {
		selector->type = PJSIP_TPSELECTOR_LISTENER;
		selector->u.listener = transport_state->factory;
	} else if (transport->type == AST_TRANSPORT_WS || transport->type == AST_TRANSPORT_WSS) {
		/* The WebSocket transport has no factory as it can not create outgoing connections, so
		 * even if an endpoint is locked to a WebSocket transport we let the PJSIP logic
		 * find the existing connection if available and use it.
		 */
	} else if (transport->flow) {
		/* This is a child of another transport, so we need to establish a new connection */
#ifdef HAVE_PJSIP_TRANSPORT_DISABLE_CONNECTION_REUSE
		selector->disable_connection_reuse = PJ_TRUE;
#else
		ast_log(LOG_WARNING, "Connection reuse could not be disabled on transport '%s' as support is not available\n",
			ast_sorcery_object_get_id(transport));
#endif
	} else {
		res = -1;
	}

	if (transport_state->flow) {
		ao2_unlock(transport_state);
	}

	ao2_ref(transport_state, -1);

	return res;
}

int ast_sip_set_tpselector_from_transport_name(const char *transport_name, pjsip_tpselector *selector)
{
	RAII_VAR(struct ast_sip_transport *, transport, NULL, ao2_cleanup);

	if (ast_strlen_zero(transport_name)) {
		return 0;
	}

	transport = ast_sorcery_retrieve_by_id(ast_sip_get_sorcery(), "transport", transport_name);
	if (!transport) {
		ast_log(LOG_ERROR, "Unable to retrieve PJSIP transport '%s'\n",
			transport_name);
		return -1;
	}

	return ast_sip_set_tpselector_from_transport(transport, selector);
}

int ast_sip_set_tpselector_from_ep_or_uri(const struct ast_sip_endpoint *endpoint,
	pjsip_sip_uri *sip_uri, pjsip_tpselector *selector)
{
	char transport_name[128];

	if (ast_sip_get_transport_name(endpoint, sip_uri, transport_name, sizeof(transport_name))) {
		return 0;
	}

	return ast_sip_set_tpselector_from_transport_name(transport_name, selector);
}

void ast_sip_tpselector_unref(pjsip_tpselector *selector)
{
	if (selector->type == PJSIP_TPSELECTOR_TRANSPORT && selector->u.transport) {
		pjsip_transport_dec_ref(selector->u.transport);
	}
}

void ast_sip_add_usereqphone(const struct ast_sip_endpoint *endpoint, pj_pool_t *pool, pjsip_uri *uri)
{
	pjsip_sip_uri *sip_uri;
	int i = 0;
	static const pj_str_t STR_PHONE = { "phone", 5 };

	if (!endpoint || !endpoint->usereqphone || (!PJSIP_URI_SCHEME_IS_SIP(uri) && !PJSIP_URI_SCHEME_IS_SIPS(uri))) {
		return;
	}

	sip_uri = pjsip_uri_get_uri(uri);

	if (!pj_strlen(&sip_uri->user)) {
		return;
	}

	if (pj_strbuf(&sip_uri->user)[0] == '+') {
		i = 1;
	}

	/* Test URI user against allowed characters in AST_DIGIT_ANY */
	for (; i < pj_strlen(&sip_uri->user); i++) {
		if (!strchr(AST_DIGIT_ANY, pj_strbuf(&sip_uri->user)[i])) {
			break;
		}
	}

	if (i < pj_strlen(&sip_uri->user)) {
		return;
	}

	sip_uri->user_param = STR_PHONE;
}

pjsip_dialog *ast_sip_create_dialog_uac(const struct ast_sip_endpoint *endpoint,
	const char *uri, const char *request_user)
{
	char enclosed_uri[PJSIP_MAX_URL_SIZE];
	pj_str_t local_uri = { "sip:temp@temp", 13 }, remote_uri, target_uri;
	pj_status_t res;
	pjsip_dialog *dlg = NULL;
	const char *outbound_proxy = endpoint->outbound_proxy;
	pjsip_tpselector selector = { .type = PJSIP_TPSELECTOR_NONE, };
	static const pj_str_t HCONTACT = { "Contact", 7 };

	snprintf(enclosed_uri, sizeof(enclosed_uri), "<%s>", uri);
	pj_cstr(&remote_uri, enclosed_uri);

	pj_cstr(&target_uri, uri);

	res = pjsip_dlg_create_uac(pjsip_ua_instance(), &local_uri, NULL, &remote_uri, &target_uri, &dlg);
	if (res == PJ_SUCCESS && !(PJSIP_URI_SCHEME_IS_SIP(dlg->target) || PJSIP_URI_SCHEME_IS_SIPS(dlg->target))) {
		/* dlg->target is a pjsip_other_uri, but it's assumed to be a
		 * pjsip_sip_uri below. Fail fast. */
		res = PJSIP_EINVALIDURI;
		pjsip_dlg_terminate(dlg);
	}
	if (res != PJ_SUCCESS) {
		if (res == PJSIP_EINVALIDURI) {
			ast_log(LOG_ERROR,
				"Endpoint '%s': Could not create dialog to invalid URI '%s'.  Is endpoint registered and reachable?\n",
				ast_sorcery_object_get_id(endpoint), uri);
		}
		return NULL;
	}

	/* We have to temporarily bump up the sess_count here so the dialog is not prematurely destroyed */
	dlg->sess_count++;

	ast_sip_dlg_set_transport(endpoint, dlg, &selector);

	if (sip_dialog_create_from(dlg->pool, &local_uri, endpoint->fromuser, endpoint->fromdomain, &remote_uri, &selector)) {
		dlg->sess_count--;
		pjsip_dlg_terminate(dlg);
		ast_sip_tpselector_unref(&selector);
		return NULL;
	}

	ast_sip_tpselector_unref(&selector);

	/* Update the dialog with the new local URI, we do it afterwards so we can use the dialog pool for construction */
	pj_strdup_with_null(dlg->pool, &dlg->local.info_str, &local_uri);
	dlg->local.info->uri = pjsip_parse_uri(dlg->pool, dlg->local.info_str.ptr, dlg->local.info_str.slen, 0);
	if (!dlg->local.info->uri) {
		ast_log(LOG_ERROR,
			"Could not parse URI '%s' for endpoint '%s'\n",
			dlg->local.info_str.ptr, ast_sorcery_object_get_id(endpoint));
		dlg->sess_count--;
		pjsip_dlg_terminate(dlg);
		return NULL;
	}

	dlg->local.contact = pjsip_parse_hdr(dlg->pool, &HCONTACT, local_uri.ptr, local_uri.slen, NULL);

	if (!ast_strlen_zero(endpoint->contact_user)) {
		pjsip_sip_uri *sip_uri;

		sip_uri = pjsip_uri_get_uri(dlg->local.contact->uri);
		pj_strdup2(dlg->pool, &sip_uri->user, endpoint->contact_user);
	}

	/* If a request user has been specified and we are permitted to change it, do so */
	if (!ast_strlen_zero(request_user)) {
		pjsip_sip_uri *sip_uri;

		if (PJSIP_URI_SCHEME_IS_SIP(dlg->target) || PJSIP_URI_SCHEME_IS_SIPS(dlg->target)) {
			sip_uri = pjsip_uri_get_uri(dlg->target);
			pj_strdup2(dlg->pool, &sip_uri->user, request_user);
		}
		if (PJSIP_URI_SCHEME_IS_SIP(dlg->remote.info->uri) || PJSIP_URI_SCHEME_IS_SIPS(dlg->remote.info->uri)) {
			sip_uri = pjsip_uri_get_uri(dlg->remote.info->uri);
			pj_strdup2(dlg->pool, &sip_uri->user, request_user);
		}
	}

	/* Add the user=phone parameter if applicable */
	ast_sip_add_usereqphone(endpoint, dlg->pool, dlg->target);
	ast_sip_add_usereqphone(endpoint, dlg->pool, dlg->remote.info->uri);

	if (!ast_strlen_zero(outbound_proxy)) {
		pjsip_route_hdr route_set, *route;
		static const pj_str_t ROUTE_HNAME = { "Route", 5 };
		pj_str_t tmp;

		pj_list_init(&route_set);

		pj_strdup2_with_null(dlg->pool, &tmp, outbound_proxy);
		if (!(route = pjsip_parse_hdr(dlg->pool, &ROUTE_HNAME, tmp.ptr, tmp.slen, NULL))) {
			ast_log(LOG_ERROR, "Could not create dialog to endpoint '%s' as outbound proxy URI '%s' is not valid\n",
				ast_sorcery_object_get_id(endpoint), outbound_proxy);
			dlg->sess_count--;
			pjsip_dlg_terminate(dlg);
			return NULL;
		}
		pj_list_insert_nodes_before(&route_set, route);

		pjsip_dlg_set_route_set(dlg, &route_set);
	}

	dlg->sess_count--;

	return dlg;
}

/*!
 * \brief Determine if a SIPS Contact header is required.
 *
 * This uses the guideline provided in RFC 3261 Section 12.1.1 to
 * determine if the Contact header must be a sips: URI.
 *
 * \param rdata The incoming dialog-starting request
 * \retval 0 SIPS not required
 * \retval 1 SIPS required
 */
static int uas_use_sips_contact(pjsip_rx_data *rdata)
{
	pjsip_rr_hdr *record_route;

	if (PJSIP_URI_SCHEME_IS_SIPS(rdata->msg_info.msg->line.req.uri)) {
		return 1;
	}

	record_route = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_RECORD_ROUTE, NULL);
	if (record_route) {
		if (PJSIP_URI_SCHEME_IS_SIPS(&record_route->name_addr)) {
			return 1;
		}
	} else {
		pjsip_contact_hdr *contact;

		contact = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_CONTACT, NULL);
		ast_assert(contact != NULL);
		if (PJSIP_URI_SCHEME_IS_SIPS(contact->uri)) {
			return 1;
		}
	}

	return 0;
}

typedef pj_status_t (*create_dlg_uac)(pjsip_user_agent *ua, pjsip_rx_data *rdata,
	const pj_str_t *contact, pjsip_dialog **p_dlg);

static pjsip_dialog *create_dialog_uas(const struct ast_sip_endpoint *endpoint,
	pjsip_rx_data *rdata, pj_status_t *status, create_dlg_uac create_fun)
{
	pjsip_dialog *dlg;
	pj_str_t contact;
	pjsip_transport_type_e type = rdata->tp_info.transport->key.type;
	pjsip_tpselector selector = { .type = PJSIP_TPSELECTOR_NONE, };
	pjsip_transport *transport;
	pjsip_contact_hdr *contact_hdr;

	ast_assert(status != NULL);

	contact_hdr = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_CONTACT, NULL);
	if (!contact_hdr || ast_sip_set_tpselector_from_ep_or_uri(endpoint, pjsip_uri_get_uri(contact_hdr->uri),
		&selector)) {
		return NULL;
	}

	transport = rdata->tp_info.transport;
	if (selector.type == PJSIP_TPSELECTOR_TRANSPORT) {
		transport = selector.u.transport;
	}
	type = transport->key.type;

	contact.ptr = pj_pool_alloc(rdata->tp_info.pool, PJSIP_MAX_URL_SIZE);
	contact.slen = pj_ansi_snprintf(contact.ptr, PJSIP_MAX_URL_SIZE,
			"<%s:%s%.*s%s:%d%s%s>",
			uas_use_sips_contact(rdata) ? "sips" : "sip",
			(type & PJSIP_TRANSPORT_IPV6) ? "[" : "",
			(int)transport->local_name.host.slen,
			transport->local_name.host.ptr,
			(type & PJSIP_TRANSPORT_IPV6) ? "]" : "",
			transport->local_name.port,
			(type != PJSIP_TRANSPORT_UDP && type != PJSIP_TRANSPORT_UDP6) ? ";transport=" : "",
			(type != PJSIP_TRANSPORT_UDP && type != PJSIP_TRANSPORT_UDP6) ? pjsip_transport_get_type_name(type) : "");

	*status = create_fun(pjsip_ua_instance(), rdata, &contact, &dlg);
	if (*status != PJ_SUCCESS) {
		char err[PJ_ERR_MSG_SIZE];

		pj_strerror(*status, err, sizeof(err));
		ast_log(LOG_ERROR, "Could not create dialog with endpoint %s. %s\n",
				ast_sorcery_object_get_id(endpoint), err);
		ast_sip_tpselector_unref(&selector);
		return NULL;
	}

	dlg->sess_count++;
	pjsip_dlg_set_transport(dlg, &selector);
	dlg->sess_count--;

	ast_sip_tpselector_unref(&selector);

	return dlg;
}

pjsip_dialog *ast_sip_create_dialog_uas(const struct ast_sip_endpoint *endpoint, pjsip_rx_data *rdata, pj_status_t *status)
{
#ifdef HAVE_PJSIP_DLG_CREATE_UAS_AND_INC_LOCK
	pjsip_dialog *dlg;

	dlg = create_dialog_uas(endpoint, rdata, status, pjsip_dlg_create_uas_and_inc_lock);
	if (dlg) {
		pjsip_dlg_dec_lock(dlg);
	}

	return dlg;
#else
	return create_dialog_uas(endpoint, rdata, status, pjsip_dlg_create_uas);
#endif
}

pjsip_dialog *ast_sip_create_dialog_uas_locked(const struct ast_sip_endpoint *endpoint,
	pjsip_rx_data *rdata, pj_status_t *status)
{
#ifdef HAVE_PJSIP_DLG_CREATE_UAS_AND_INC_LOCK
	return create_dialog_uas(endpoint, rdata, status, pjsip_dlg_create_uas_and_inc_lock);
#else
	/*
	 * This is put here in order to be compatible with older versions of pjproject.
	 * Best we can do in this case is immediately lock after getting the dialog.
	 * However, that does leave a "gap" between creating and locking.
	 */
	pjsip_dialog *dlg;

	dlg = create_dialog_uas(endpoint, rdata, status, pjsip_dlg_create_uas);
	if (dlg) {
		pjsip_dlg_inc_lock(dlg);
	}

	return dlg;
#endif
 }

int ast_sip_create_rdata_with_contact(pjsip_rx_data *rdata, char *packet, const char *src_name, int src_port,
	char *transport_type, const char *local_name, int local_port, const char *contact)
{
	pj_str_t tmp;

	/*
	 * Initialize the error list in case there is a parse error
	 * in the given packet.
	 */
	pj_list_init(&rdata->msg_info.parse_err);

	rdata->tp_info.transport = PJ_POOL_ZALLOC_T(rdata->tp_info.pool, pjsip_transport);
	if (!rdata->tp_info.transport) {
		return -1;
	}

	ast_copy_string(rdata->pkt_info.packet, packet, sizeof(rdata->pkt_info.packet));
	ast_copy_string(rdata->pkt_info.src_name, src_name, sizeof(rdata->pkt_info.src_name));
	rdata->pkt_info.src_port = src_port;
	pj_sockaddr_parse(pj_AF_UNSPEC(), 0, pj_cstr(&tmp, src_name), &rdata->pkt_info.src_addr);
	pj_sockaddr_set_port(&rdata->pkt_info.src_addr, src_port);

	pjsip_parse_rdata(packet, strlen(packet), rdata);
	if (!rdata->msg_info.msg || !pj_list_empty(&rdata->msg_info.parse_err)) {
		return -1;
	}

	if (!ast_strlen_zero(contact)) {
		pjsip_contact_hdr *contact_hdr;

		contact_hdr = pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_CONTACT, NULL);
		if (contact_hdr) {
			contact_hdr->uri = pjsip_parse_uri(rdata->tp_info.pool, (char *)contact,
				strlen(contact), PJSIP_PARSE_URI_AS_NAMEADDR);
			if (!contact_hdr->uri) {
				ast_log(LOG_WARNING, "Unable to parse contact URI from '%s'.\n", contact);
				return -1;
			}
		}
	}

	pj_strdup2(rdata->tp_info.pool, &rdata->msg_info.via->recvd_param, rdata->pkt_info.src_name);
	rdata->msg_info.via->rport_param = -1;

	rdata->tp_info.transport->key.type = pjsip_transport_get_type_from_name(pj_cstr(&tmp, transport_type));
	rdata->tp_info.transport->type_name = transport_type;
	pj_strdup2(rdata->tp_info.pool, &rdata->tp_info.transport->local_name.host, local_name);
	rdata->tp_info.transport->local_name.port = local_port;

	return 0;
}

int ast_sip_create_rdata(pjsip_rx_data *rdata, char *packet, const char *src_name, int src_port,
	char *transport_type, const char *local_name, int local_port)
{
	return ast_sip_create_rdata_with_contact(rdata, packet, src_name, src_port, transport_type,
		local_name, local_port, NULL);
}

/* PJSIP doesn't know about the INFO method, so we have to define it ourselves */
static const pjsip_method info_method = {PJSIP_OTHER_METHOD, {"INFO", 4} };
static const pjsip_method message_method = {PJSIP_OTHER_METHOD, {"MESSAGE", 7} };

static struct {
	const char *method;
	const pjsip_method *pmethod;
} methods [] = {
	{ "INVITE", &pjsip_invite_method },
	{ "CANCEL", &pjsip_cancel_method },
	{ "ACK", &pjsip_ack_method },
	{ "BYE", &pjsip_bye_method },
	{ "REGISTER", &pjsip_register_method },
	{ "OPTIONS", &pjsip_options_method },
	{ "SUBSCRIBE", &pjsip_subscribe_method },
	{ "NOTIFY", &pjsip_notify_method },
	{ "PUBLISH", &pjsip_publish_method },
	{ "INFO", &info_method },
	{ "MESSAGE", &message_method },
};

static const pjsip_method *get_pjsip_method(const char *method)
{
	int i;
	for (i = 0; i < ARRAY_LEN(methods); ++i) {
		if (!strcmp(method, methods[i].method)) {
			return methods[i].pmethod;
		}
	}
	return NULL;
}

static int create_in_dialog_request(const pjsip_method *method, struct pjsip_dialog *dlg, pjsip_tx_data **tdata)
{
	if (pjsip_dlg_create_request(dlg, method, -1, tdata) != PJ_SUCCESS) {
		ast_log(LOG_WARNING, "Unable to create in-dialog request.\n");
		return -1;
	}

	return 0;
}

static pj_bool_t supplement_on_rx_request(pjsip_rx_data *rdata);
static pjsip_module supplement_module = {
	.name = { "Out of dialog supplement hook", 29 },
	.id = -1,
	.priority = PJSIP_MOD_PRIORITY_APPLICATION - 1,
	.on_rx_request = supplement_on_rx_request,
};

static int create_out_of_dialog_request(const pjsip_method *method, struct ast_sip_endpoint *endpoint,
		const char *uri, struct ast_sip_contact *provided_contact, pjsip_tx_data **tdata)
{
	RAII_VAR(struct ast_sip_contact *, contact, ao2_bump(provided_contact), ao2_cleanup);
	pj_str_t remote_uri;
	pj_str_t from;
	pj_pool_t *pool;
	pjsip_tpselector selector = { .type = PJSIP_TPSELECTOR_NONE, };
	pjsip_uri *sip_uri;
	const char *fromuser;

	if (ast_strlen_zero(uri)) {
		if (!endpoint && (!contact || ast_strlen_zero(contact->uri))) {
			ast_log(LOG_ERROR, "An endpoint and/or uri must be specified\n");
			return -1;
		}

		if (!contact) {
			contact = ast_sip_location_retrieve_contact_from_aor_list(endpoint->aors);
		}
		if (!contact || ast_strlen_zero(contact->uri)) {
			ast_log(LOG_WARNING, "Unable to retrieve contact for endpoint %s\n",
					ast_sorcery_object_get_id(endpoint));
			return -1;
		}

		pj_cstr(&remote_uri, contact->uri);
	} else {
		pj_cstr(&remote_uri, uri);
	}

	pool = pjsip_endpt_create_pool(ast_sip_get_pjsip_endpoint(), "Outbound request", 256, 256);

	if (!pool) {
		ast_log(LOG_ERROR, "Unable to create PJLIB memory pool\n");
		return -1;
	}

	sip_uri = pjsip_parse_uri(pool, remote_uri.ptr, remote_uri.slen, 0);
	if (!sip_uri || (!PJSIP_URI_SCHEME_IS_SIP(sip_uri) && !PJSIP_URI_SCHEME_IS_SIPS(sip_uri))) {
		ast_log(LOG_ERROR, "Unable to create outbound %.*s request to endpoint %s as URI '%s' is not valid\n",
			(int) pj_strlen(&method->name), pj_strbuf(&method->name),
			endpoint ? ast_sorcery_object_get_id(endpoint) : "<none>",
			pj_strbuf(&remote_uri));
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
		return -1;
	}

	ast_sip_set_tpselector_from_ep_or_uri(endpoint, pjsip_uri_get_uri(sip_uri), &selector);

	fromuser = endpoint ? (!ast_strlen_zero(endpoint->fromuser) ? endpoint->fromuser : ast_sorcery_object_get_id(endpoint)) : NULL;
	if (sip_dialog_create_from(pool, &from, fromuser,
				endpoint ? endpoint->fromdomain : NULL, &remote_uri, &selector)) {
		ast_log(LOG_ERROR, "Unable to create From header for %.*s request to endpoint %s\n",
				(int) pj_strlen(&method->name), pj_strbuf(&method->name),
				endpoint ? ast_sorcery_object_get_id(endpoint) : "<none>");
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
		ast_sip_tpselector_unref(&selector);
		return -1;
	}

	if (pjsip_endpt_create_request(ast_sip_get_pjsip_endpoint(), method, &remote_uri,
			&from, &remote_uri, &from, NULL, -1, NULL, tdata) != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Unable to create outbound %.*s request to endpoint %s\n",
				(int) pj_strlen(&method->name), pj_strbuf(&method->name),
				endpoint ? ast_sorcery_object_get_id(endpoint) : "<none>");
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
		ast_sip_tpselector_unref(&selector);
		return -1;
	}

	pjsip_tx_data_set_transport(*tdata, &selector);

	ast_sip_tpselector_unref(&selector);

	if (endpoint && !ast_strlen_zero(endpoint->contact_user)){
		pjsip_contact_hdr *contact_hdr;
		pjsip_sip_uri *contact_uri;
		static const pj_str_t HCONTACT = { "Contact", 7 };
		static const pj_str_t HCONTACTSHORT = { "m", 1 };

		contact_hdr = pjsip_msg_find_hdr_by_names((*tdata)->msg, &HCONTACT, &HCONTACTSHORT, NULL);
		if (contact_hdr) {
			contact_uri = pjsip_uri_get_uri(contact_hdr->uri);
			pj_strdup2((*tdata)->pool, &contact_uri->user, endpoint->contact_user);
		}
	}

	/* Add the user=phone parameter if applicable */
	ast_sip_add_usereqphone(endpoint, (*tdata)->pool, (*tdata)->msg->line.req.uri);

	/* If an outbound proxy is specified on the endpoint apply it to this request */
	if (endpoint && !ast_strlen_zero(endpoint->outbound_proxy) &&
		ast_sip_set_outbound_proxy((*tdata), endpoint->outbound_proxy)) {
		ast_log(LOG_ERROR, "Unable to apply outbound proxy on request %.*s to endpoint %s as outbound proxy URI '%s' is not valid\n",
			(int) pj_strlen(&method->name), pj_strbuf(&method->name), ast_sorcery_object_get_id(endpoint),
			endpoint->outbound_proxy);
		pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
		return -1;
	}

	ast_sip_mod_data_set((*tdata)->pool, (*tdata)->mod_data, supplement_module.id, MOD_DATA_CONTACT, ao2_bump(contact));

	/* We can release this pool since request creation copied all the necessary
	 * data into the outbound request's pool
	 */
	pjsip_endpt_release_pool(ast_sip_get_pjsip_endpoint(), pool);
	return 0;
}

int ast_sip_create_request(const char *method, struct pjsip_dialog *dlg,
		struct ast_sip_endpoint *endpoint, const char *uri,
		struct ast_sip_contact *contact, pjsip_tx_data **tdata)
{
	const pjsip_method *pmethod = get_pjsip_method(method);

	if (!pmethod) {
		ast_log(LOG_WARNING, "Unknown method '%s'. Cannot send request\n", method);
		return -1;
	}

	if (dlg) {
		return create_in_dialog_request(pmethod, dlg, tdata);
	} else {
		ast_assert(endpoint != NULL);
		return create_out_of_dialog_request(pmethod, endpoint, uri, contact, tdata);
	}
}

AST_RWLIST_HEAD_STATIC(supplements, ast_sip_supplement);

void ast_sip_register_supplement(struct ast_sip_supplement *supplement)
{
	struct ast_sip_supplement *iter;
	int inserted = 0;
	SCOPED_LOCK(lock, &supplements, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);

	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&supplements, iter, next) {
		if (iter->priority > supplement->priority) {
			AST_RWLIST_INSERT_BEFORE_CURRENT(supplement, next);
			inserted = 1;
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;

	if (!inserted) {
		AST_RWLIST_INSERT_TAIL(&supplements, supplement, next);
	}
}

void ast_sip_unregister_supplement(struct ast_sip_supplement *supplement)
{
	struct ast_sip_supplement *iter;
	SCOPED_LOCK(lock, &supplements, AST_RWLIST_WRLOCK, AST_RWLIST_UNLOCK);

	AST_RWLIST_TRAVERSE_SAFE_BEGIN(&supplements, iter, next) {
		if (supplement == iter) {
			AST_RWLIST_REMOVE_CURRENT(next);
			break;
		}
	}
	AST_RWLIST_TRAVERSE_SAFE_END;
}

static int send_in_dialog_request(pjsip_tx_data *tdata, struct pjsip_dialog *dlg)
{
	if (pjsip_dlg_send_request(dlg, tdata, -1, NULL) != PJ_SUCCESS) {
		ast_log(LOG_WARNING, "Unable to send in-dialog request.\n");
		return -1;
	}
	return 0;
}

static pj_bool_t does_method_match(const pj_str_t *message_method, const char *supplement_method)
{
	pj_str_t method;

	if (ast_strlen_zero(supplement_method)) {
		return PJ_TRUE;
	}

	pj_cstr(&method, supplement_method);

	return pj_stristr(&method, message_method) ? PJ_TRUE : PJ_FALSE;
}

#define TIMER_INACTIVE		0
#define TIMEOUT_TIMER2		5

/*! \brief Structure to hold information about an outbound request */
struct send_request_data {
	/*! The endpoint associated with this request */
	struct ast_sip_endpoint *endpoint;
	/*! Information to be provided to the callback upon receipt of a response */
	void *token;
	/*! The callback to be called upon receipt of a response */
	void (*callback)(void *token, pjsip_event *e);
	/*! Number of challenges received. */
	unsigned int challenge_count;
};

static void send_request_data_destroy(void *obj)
{
	struct send_request_data *req_data = obj;

	ao2_cleanup(req_data->endpoint);
}

static struct send_request_data *send_request_data_alloc(struct ast_sip_endpoint *endpoint,
	void *token, void (*callback)(void *token, pjsip_event *e))
{
	struct send_request_data *req_data;

	req_data = ao2_alloc_options(sizeof(*req_data), send_request_data_destroy,
		AO2_ALLOC_OPT_LOCK_NOLOCK);
	if (!req_data) {
		return NULL;
	}

	req_data->endpoint = ao2_bump(endpoint);
	req_data->token = token;
	req_data->callback = callback;

	return req_data;
}

struct send_request_wrapper {
	/*! Information to be provided to the callback upon receipt of a response */
	void *token;
	/*! The callback to be called upon receipt of a response */
	void (*callback)(void *token, pjsip_event *e);
	/*! Non-zero when the callback is called. */
	unsigned int cb_called;
	/*! Non-zero if endpt_send_request_cb() was called. */
	unsigned int send_cb_called;
	/*! Timeout timer. */
	pj_timer_entry *timeout_timer;
	/*! Original timeout. */
	pj_int32_t timeout;
	/*! The transmit data. */
	pjsip_tx_data *tdata;
};

/*! \internal This function gets called by pjsip when the transaction ends,
 * even if it timed out.  The lock prevents a race condition if both the pjsip
 * transaction timer and our own timer expire simultaneously.
 */
static void endpt_send_request_cb(void *token, pjsip_event *e)
{
	struct send_request_wrapper *req_wrapper = token;
	unsigned int cb_called;

	/*
	 * Needed because we cannot otherwise tell if this callback was
	 * called when pjsip_endpt_send_request() returns error.
	 */
	req_wrapper->send_cb_called = 1;

	if (e->body.tsx_state.type == PJSIP_EVENT_TIMER) {
		ast_debug(2, "%p: PJSIP tsx timer expired\n", req_wrapper);

		if (req_wrapper->timeout_timer
			&& req_wrapper->timeout_timer->id != TIMEOUT_TIMER2) {
			ast_debug(3, "%p: Timeout already handled\n", req_wrapper);
			ao2_ref(req_wrapper, -1);
			return;
		}
	} else {
		ast_debug(2, "%p: PJSIP tsx response received\n", req_wrapper);
	}

	ao2_lock(req_wrapper);

	/* It's possible that our own timer was already processing while
	 * we were waiting on the lock so check the timer id.  If it's
	 * still TIMER2 then we still need to process.
	 */
	if (req_wrapper->timeout_timer
		&& req_wrapper->timeout_timer->id == TIMEOUT_TIMER2) {
		int timers_cancelled = 0;

		ast_debug(3, "%p: Cancelling timer\n", req_wrapper);

		timers_cancelled = pj_timer_heap_cancel_if_active(
			pjsip_endpt_get_timer_heap(ast_sip_get_pjsip_endpoint()),
			req_wrapper->timeout_timer, TIMER_INACTIVE);
		if (timers_cancelled > 0) {
			/* If the timer was cancelled the callback will never run so
			 * clean up its reference to the wrapper.
			 */
			ast_debug(3, "%p: Timer cancelled\n", req_wrapper);
			ao2_ref(req_wrapper, -1);
		} else {
			/*
			 * If it wasn't cancelled, it MAY be in the callback already
			 * waiting on the lock.  When we release the lock, it will
			 * now know not to proceed.
			 */
			ast_debug(3, "%p: Timer already expired\n", req_wrapper);
		}
	}

	cb_called = req_wrapper->cb_called;
	req_wrapper->cb_called = 1;
	ao2_unlock(req_wrapper);

	/* It's possible that our own timer expired and called the callbacks
	 * so no need to call them again.
	 */
	if (!cb_called && req_wrapper->callback) {
		req_wrapper->callback(req_wrapper->token, e);
		ast_debug(2, "%p: Callbacks executed\n", req_wrapper);
	}

	ao2_ref(req_wrapper, -1);
}

/*! \internal This function gets called by our own timer when it expires.
 * If the timer is cancelled however, the function does NOT get called.
 * The lock prevents a race condition if both the pjsip transaction timer
 * and our own timer expire simultaneously.
 */
static void send_request_timer_callback(pj_timer_heap_t *theap, pj_timer_entry *entry)
{
	struct send_request_wrapper *req_wrapper = entry->user_data;
	unsigned int cb_called;

	ast_debug(2, "%p: Internal tsx timer expired after %d msec\n",
		req_wrapper, req_wrapper->timeout);

	ao2_lock(req_wrapper);
	/*
	 * If the id is not TIMEOUT_TIMER2 then the timer was cancelled
	 * before we got the lock or it was already handled so just clean up.
	 */
	if (entry->id != TIMEOUT_TIMER2) {
		ao2_unlock(req_wrapper);
		ast_debug(3, "%p: Timeout already handled\n", req_wrapper);
		ao2_ref(req_wrapper, -1);
		return;
	}
	entry->id = TIMER_INACTIVE;

	ast_debug(3, "%p: Timer handled here\n", req_wrapper);

	cb_called = req_wrapper->cb_called;
	req_wrapper->cb_called = 1;
	ao2_unlock(req_wrapper);

	if (!cb_called && req_wrapper->callback) {
		pjsip_event event;

		PJSIP_EVENT_INIT_TX_MSG(event, req_wrapper->tdata);
		event.body.tsx_state.type = PJSIP_EVENT_TIMER;

		req_wrapper->callback(req_wrapper->token, &event);
		ast_debug(2, "%p: Callbacks executed\n", req_wrapper);
	}

	ao2_ref(req_wrapper, -1);
}

static void send_request_wrapper_destructor(void *obj)
{
	struct send_request_wrapper *req_wrapper = obj;

	pjsip_tx_data_dec_ref(req_wrapper->tdata);
	ast_debug(2, "%p: wrapper destroyed\n", req_wrapper);
}

static pj_status_t endpt_send_request(struct ast_sip_endpoint *endpoint,
	pjsip_tx_data *tdata, pj_int32_t timeout, void *token, pjsip_endpt_send_callback cb)
{
	struct send_request_wrapper *req_wrapper;
	pj_status_t ret_val;
	pjsip_endpoint *endpt = ast_sip_get_pjsip_endpoint();

	if (!cb && token) {
		/* Silly.  Without a callback we cannot do anything with token. */
		pjsip_tx_data_dec_ref(tdata);
		return PJ_EINVAL;
	}

	/* Create wrapper to detect if the callback was actually called on an error. */
	req_wrapper = ao2_alloc(sizeof(*req_wrapper), send_request_wrapper_destructor);
	if (!req_wrapper) {
		pjsip_tx_data_dec_ref(tdata);
		return PJ_ENOMEM;
	}

	ast_debug(2, "%p: Wrapper created\n", req_wrapper);

	req_wrapper->token = token;
	req_wrapper->callback = cb;
	req_wrapper->timeout = timeout;
	req_wrapper->timeout_timer = NULL;
	req_wrapper->tdata = tdata;
	/* Add a reference to tdata.  The wrapper destructor cleans it up. */
	pjsip_tx_data_add_ref(tdata);

	if (timeout > 0) {
		pj_time_val timeout_timer_val = { timeout / 1000, timeout % 1000 };

		req_wrapper->timeout_timer = PJ_POOL_ALLOC_T(tdata->pool, pj_timer_entry);

		ast_debug(2, "%p: Set timer to %d msec\n", req_wrapper, timeout);

		pj_timer_entry_init(req_wrapper->timeout_timer, TIMEOUT_TIMER2,
			req_wrapper, send_request_timer_callback);

		/* We need to insure that the wrapper and tdata are available if/when the
		 * timer callback is executed.
		 */
		ao2_ref(req_wrapper, +1);
		ret_val = pj_timer_heap_schedule(pjsip_endpt_get_timer_heap(endpt),
			req_wrapper->timeout_timer, &timeout_timer_val);
		if (ret_val != PJ_SUCCESS) {
			ast_log(LOG_ERROR,
				"Failed to set timer.  Not sending %.*s request to endpoint %s.\n",
				(int) pj_strlen(&tdata->msg->line.req.method.name),
				pj_strbuf(&tdata->msg->line.req.method.name),
				endpoint ? ast_sorcery_object_get_id(endpoint) : "<unknown>");
			ao2_t_ref(req_wrapper, -2, "Drop timer and routine ref");
			pjsip_tx_data_dec_ref(tdata);
			return ret_val;
		}
	}

	/* We need to insure that the wrapper and tdata are available when the
	 * transaction callback is executed.
	 */
	ao2_ref(req_wrapper, +1);
	ret_val = pjsip_endpt_send_request(endpt, tdata, -1, req_wrapper, endpt_send_request_cb);
	if (ret_val != PJ_SUCCESS) {
		char errmsg[PJ_ERR_MSG_SIZE];

		if (!req_wrapper->send_cb_called) {
			/* endpt_send_request_cb is not expected to ever be called now. */
			ao2_ref(req_wrapper, -1);
		}

		/* Complain of failure to send the request. */
		pj_strerror(ret_val, errmsg, sizeof(errmsg));
		ast_log(LOG_ERROR, "Error %d '%s' sending %.*s request to endpoint %s\n",
			(int) ret_val, errmsg, (int) pj_strlen(&tdata->msg->line.req.method.name),
			pj_strbuf(&tdata->msg->line.req.method.name),
			endpoint ? ast_sorcery_object_get_id(endpoint) : "<unknown>");

		if (timeout > 0) {
			int timers_cancelled;

			ao2_lock(req_wrapper);
			timers_cancelled = pj_timer_heap_cancel_if_active(
				pjsip_endpt_get_timer_heap(endpt),
				req_wrapper->timeout_timer, TIMER_INACTIVE);
			if (timers_cancelled > 0) {
				ao2_ref(req_wrapper, -1);
			}

			/* Was the callback called? */
			if (req_wrapper->cb_called) {
				/*
				 * Yes so we cannot report any error.  The callback
				 * has already freed any resources associated with
				 * token.
				 */
				ret_val = PJ_SUCCESS;
			} else {
				/*
				 * No so we claim it is called so our caller can free
				 * any resources associated with token because of
				 * failure.
				 */
				req_wrapper->cb_called = 1;
			}
			ao2_unlock(req_wrapper);
		} else if (req_wrapper->cb_called) {
			/*
			 * We cannot report any error.  The callback has
			 * already freed any resources associated with
			 * token.
			 */
			ret_val = PJ_SUCCESS;
		}
	}

	ao2_ref(req_wrapper, -1);
	return ret_val;
}

int ast_sip_failover_request(pjsip_tx_data *tdata)
{
	pjsip_via_hdr *via;

	if (!tdata || !tdata->dest_info.addr.count
		|| (tdata->dest_info.cur_addr == tdata->dest_info.addr.count - 1)) {
		/* No more addresses to try */
		return 0;
	}

	/* Try next address */
	++tdata->dest_info.cur_addr;

	via = (pjsip_via_hdr*)pjsip_msg_find_hdr(tdata->msg, PJSIP_H_VIA, NULL);
	via->branch_param.slen = 0;

	pjsip_tx_data_invalidate_msg(tdata);

	return 1;
}

static void send_request_cb(void *token, pjsip_event *e);

static int check_request_status(struct send_request_data *req_data, pjsip_event *e)
{
	struct ast_sip_endpoint *endpoint;
	pjsip_transaction *tsx;
	pjsip_tx_data *tdata;
	int res = 0;

	if (!(endpoint = ao2_bump(req_data->endpoint))) {
		return 0;
	}

	tsx = e->body.tsx_state.tsx;

	switch (tsx->status_code) {
	case 401:
	case 407:
		/* Resend the request with a challenge response if we are challenged. */
		res = ++req_data->challenge_count < MAX_RX_CHALLENGES /* Not in a challenge loop */
			&& !ast_sip_create_request_with_auth(&endpoint->outbound_auths,
				e->body.tsx_state.src.rdata, tsx->last_tx, &tdata);
		break;
	case 408:
	case 503:
		if ((res = ast_sip_failover_request(tsx->last_tx))) {
			tdata = tsx->last_tx;
			/*
			 * Bump the ref since it will be on a new transaction and
			 * we don't want it to go away along with the old transaction.
			 */
			pjsip_tx_data_add_ref(tdata);
		}
		break;
	}

	if (res) {
		res = endpt_send_request(endpoint, tdata, -1,
					 req_data, send_request_cb) == PJ_SUCCESS;
	}

	ao2_ref(endpoint, -1);
	return res;
}

static void send_request_cb(void *token, pjsip_event *e)
{
	struct send_request_data *req_data = token;
	pjsip_rx_data *challenge;
	struct ast_sip_supplement *supplement;

	if (e->type == PJSIP_EVENT_TSX_STATE) {
		switch(e->body.tsx_state.type) {
		case PJSIP_EVENT_TRANSPORT_ERROR:
		case PJSIP_EVENT_TIMER:
			/*
			 * Check the request status on transport error or timeout. A transport
			 * error can occur when a TCP socket closes and that can be the result
			 * of a 503. Also we may need to failover on a timeout (408).
			 */
			if (check_request_status(req_data, e)) {
				return;
			}
			break;
		case PJSIP_EVENT_RX_MSG:
			challenge = e->body.tsx_state.src.rdata;

			/*
			 * Call any supplements that want to know about a response
			 * with any received data.
			 */
			AST_RWLIST_RDLOCK(&supplements);
			AST_LIST_TRAVERSE(&supplements, supplement, next) {
				if (supplement->incoming_response
					&& does_method_match(&challenge->msg_info.cseq->method.name,
						supplement->method)) {
					supplement->incoming_response(req_data->endpoint, challenge);
				}
			}
			AST_RWLIST_UNLOCK(&supplements);

			if (check_request_status(req_data, e)) {
				/*
				 * Request with challenge response or failover sent.
				 * Passed our req_data ref to the new request.
				 */
				return;
			}
			break;
		default:
			ast_log(LOG_ERROR, "Unexpected PJSIP event %u\n", e->body.tsx_state.type);
			break;
		}
	}

	if (req_data->callback) {
		req_data->callback(req_data->token, e);
	}
	ao2_ref(req_data, -1);
}

int ast_sip_send_out_of_dialog_request(pjsip_tx_data *tdata,
	struct ast_sip_endpoint *endpoint, int timeout, void *token,
	void (*callback)(void *token, pjsip_event *e))
{
	struct ast_sip_supplement *supplement;
	struct send_request_data *req_data;
	struct ast_sip_contact *contact;

	req_data = send_request_data_alloc(endpoint, token, callback);
	if (!req_data) {
		pjsip_tx_data_dec_ref(tdata);
		return -1;
	}

	if (endpoint) {
		ast_sip_message_apply_transport(endpoint->transport, tdata);
	}

	contact = ast_sip_mod_data_get(tdata->mod_data, supplement_module.id, MOD_DATA_CONTACT);

	AST_RWLIST_RDLOCK(&supplements);
	AST_LIST_TRAVERSE(&supplements, supplement, next) {
		if (supplement->outgoing_request
			&& does_method_match(&tdata->msg->line.req.method.name, supplement->method)) {
			supplement->outgoing_request(endpoint, contact, tdata);
		}
	}
	AST_RWLIST_UNLOCK(&supplements);

	ast_sip_mod_data_set(tdata->pool, tdata->mod_data, supplement_module.id, MOD_DATA_CONTACT, NULL);
	ao2_cleanup(contact);

	if (endpt_send_request(endpoint, tdata, timeout, req_data, send_request_cb)
		!= PJ_SUCCESS) {
		ao2_cleanup(req_data);
		return -1;
	}

	return 0;
}

int ast_sip_send_request(pjsip_tx_data *tdata, struct pjsip_dialog *dlg,
	struct ast_sip_endpoint *endpoint, void *token,
	void (*callback)(void *token, pjsip_event *e))
{
	ast_assert(tdata->msg->type == PJSIP_REQUEST_MSG);

	if (dlg) {
		return send_in_dialog_request(tdata, dlg);
	} else {
		return ast_sip_send_out_of_dialog_request(tdata, endpoint, -1, token, callback);
	}
}

int ast_sip_set_outbound_proxy(pjsip_tx_data *tdata, const char *proxy)
{
	pjsip_route_hdr *route;
	static const pj_str_t ROUTE_HNAME = { "Route", 5 };
	pj_str_t tmp;

	pj_strdup2_with_null(tdata->pool, &tmp, proxy);
	if (!(route = pjsip_parse_hdr(tdata->pool, &ROUTE_HNAME, tmp.ptr, tmp.slen, NULL))) {
		return -1;
	}

	pj_list_insert_nodes_before(&tdata->msg->hdr, (pjsip_hdr*)route);

	return 0;
}

int ast_sip_add_header(pjsip_tx_data *tdata, const char *name, const char *value)
{
	pj_str_t hdr_name;
	pj_str_t hdr_value;
	pjsip_generic_string_hdr *hdr;

	pj_cstr(&hdr_name, name);
	pj_cstr(&hdr_value, value);

	hdr = pjsip_generic_string_hdr_create(tdata->pool, &hdr_name, &hdr_value);

	pjsip_msg_add_hdr(tdata->msg, (pjsip_hdr *) hdr);
	return 0;
}

static pjsip_msg_body *ast_body_to_pjsip_body(pj_pool_t *pool, const struct ast_sip_body *body)
{
	pj_str_t type;
	pj_str_t subtype;
	pj_str_t body_text;

	pj_cstr(&type, body->type);
	pj_cstr(&subtype, body->subtype);
	pj_cstr(&body_text, body->body_text);

	return pjsip_msg_body_create(pool, &type, &subtype, &body_text);
}

int ast_sip_add_body(pjsip_tx_data *tdata, const struct ast_sip_body *body)
{
	pjsip_msg_body *pjsip_body = ast_body_to_pjsip_body(tdata->pool, body);
	tdata->msg->body = pjsip_body;
	return 0;
}

int ast_sip_add_body_multipart(pjsip_tx_data *tdata, const struct ast_sip_body *bodies[], int num_bodies)
{
	int i;
	/* NULL for type and subtype automatically creates "multipart/mixed" */
	pjsip_msg_body *body = pjsip_multipart_create(tdata->pool, NULL, NULL);

	for (i = 0; i < num_bodies; ++i) {
		pjsip_multipart_part *part = pjsip_multipart_create_part(tdata->pool);
		part->body = ast_body_to_pjsip_body(tdata->pool, bodies[i]);
		pjsip_multipart_add_part(tdata->pool, body, part);
	}

	tdata->msg->body = body;
	return 0;
}

int ast_sip_append_body(pjsip_tx_data *tdata, const char *body_text)
{
	size_t combined_size = strlen(body_text) + tdata->msg->body->len;
	struct ast_str *body_buffer = ast_str_alloca(combined_size);

	ast_str_set(&body_buffer, 0, "%.*s%s", (int) tdata->msg->body->len, (char *) tdata->msg->body->data, body_text);

	tdata->msg->body->data = pj_pool_alloc(tdata->pool, combined_size);
	pj_memcpy(tdata->msg->body->data, ast_str_buffer(body_buffer), combined_size);
	tdata->msg->body->len = combined_size;

	return 0;
}

struct ast_taskprocessor *ast_sip_create_serializer_group(const char *name, struct ast_serializer_shutdown_group *shutdown_group)
{
	return ast_threadpool_serializer_group(name, sip_threadpool, shutdown_group);
}

struct ast_taskprocessor *ast_sip_create_serializer(const char *name)
{
	return ast_sip_create_serializer_group(name, NULL);
}

int ast_sip_push_task(struct ast_taskprocessor *serializer, int (*sip_task)(void *), void *task_data)
{
	if (!serializer) {
		serializer = ast_serializer_pool_get(sip_serializer_pool);
	}

	return ast_taskprocessor_push(serializer, sip_task, task_data);
}

struct sync_task_data {
	ast_mutex_t lock;
	ast_cond_t cond;
	int complete;
	int fail;
	int (*task)(void *);
	void *task_data;
};

static int sync_task(void *data)
{
	struct sync_task_data *std = data;
	int ret;

	std->fail = std->task(std->task_data);

	/*
	 * Once we unlock std->lock after signaling, we cannot access
	 * std again.  The thread waiting within ast_sip_push_task_wait()
	 * is free to continue and release its local variable (std).
	 */
	ast_mutex_lock(&std->lock);
	std->complete = 1;
	ast_cond_signal(&std->cond);
	ret = std->fail;
	ast_mutex_unlock(&std->lock);
	return ret;
}

static int ast_sip_push_task_wait(struct ast_taskprocessor *serializer, int (*sip_task)(void *), void *task_data)
{
	/* This method is an onion */
	struct sync_task_data std;

	memset(&std, 0, sizeof(std));
	ast_mutex_init(&std.lock);
	ast_cond_init(&std.cond, NULL);
	std.task = sip_task;
	std.task_data = task_data;

	if (ast_sip_push_task(serializer, sync_task, &std)) {
		ast_mutex_destroy(&std.lock);
		ast_cond_destroy(&std.cond);
		return -1;
	}

	ast_mutex_lock(&std.lock);
	while (!std.complete) {
		ast_cond_wait(&std.cond, &std.lock);
	}
	ast_mutex_unlock(&std.lock);

	ast_mutex_destroy(&std.lock);
	ast_cond_destroy(&std.cond);
	return std.fail;
}

int ast_sip_push_task_wait_servant(struct ast_taskprocessor *serializer, int (*sip_task)(void *), void *task_data)
{
	if (ast_sip_thread_is_servant()) {
		return sip_task(task_data);
	}

	return ast_sip_push_task_wait(serializer, sip_task, task_data);
}

int ast_sip_push_task_synchronous(struct ast_taskprocessor *serializer, int (*sip_task)(void *), void *task_data)
{
	return ast_sip_push_task_wait_servant(serializer, sip_task, task_data);
}

int ast_sip_push_task_wait_serializer(struct ast_taskprocessor *serializer, int (*sip_task)(void *), void *task_data)
{
	if (!serializer) {
		/* Caller doesn't care which PJSIP serializer the task executes under. */
		serializer = ast_serializer_pool_get(sip_serializer_pool);
		if (!serializer) {
			/* No serializer picked to execute the task */
			return -1;
		}
	}
	if (ast_taskprocessor_is_task(serializer)) {
		/*
		 * We are the requested serializer so we must execute
		 * the task now or deadlock waiting on ourself to
		 * execute it.
		 */
		return sip_task(task_data);
	}

	return ast_sip_push_task_wait(serializer, sip_task, task_data);
}

void ast_copy_pj_str(char *dest, const pj_str_t *src, size_t size)
{
	size_t chars_to_copy = MIN(size - 1, pj_strlen(src));
	memcpy(dest, pj_strbuf(src), chars_to_copy);
	dest[chars_to_copy] = '\0';
}

int ast_copy_pj_str2(char **dest, const pj_str_t *src)
{
	int res = ast_asprintf(dest, "%.*s", (int)pj_strlen(src), pj_strbuf(src));

	if (res < 0) {
		*dest = NULL;
	}

	return res;
}


int ast_sip_is_content_type(pjsip_media_type *content_type, char *type, char *subtype)
{
	pjsip_media_type compare;

	if (!content_type) {
		return 0;
	}

	pjsip_media_type_init2(&compare, type, subtype);

	return pjsip_media_type_cmp(content_type, &compare, 0) ? 0 : -1;
}

pj_caching_pool caching_pool;
pj_pool_t *memory_pool;
pj_thread_t *monitor_thread;
static int monitor_continue;

static void *monitor_thread_exec(void *endpt)
{
	while (monitor_continue) {
		const pj_time_val delay = {0, 10};
		pjsip_endpt_handle_events(ast_pjsip_endpoint, &delay);
	}
	return NULL;
}

static void stop_monitor_thread(void)
{
	monitor_continue = 0;
	pj_thread_join(monitor_thread);
}

AST_THREADSTORAGE(pj_thread_storage);
AST_THREADSTORAGE(servant_id_storage);
#define SIP_SERVANT_ID 0x5E2F1D

static void sip_thread_start(void)
{
	pj_thread_desc *desc;
	pj_thread_t *thread;
	uint32_t *servant_id;

	servant_id = ast_threadstorage_get(&servant_id_storage, sizeof(*servant_id));
	if (!servant_id) {
		ast_log(LOG_ERROR, "Could not set SIP servant ID in thread-local storage.\n");
		return;
	}
	*servant_id = SIP_SERVANT_ID;

	desc = ast_threadstorage_get(&pj_thread_storage, sizeof(pj_thread_desc));
	if (!desc) {
		ast_log(LOG_ERROR, "Could not get thread desc from thread-local storage. Expect awful things to occur\n");
		return;
	}
	pj_bzero(*desc, sizeof(*desc));

	if (pj_thread_register("Asterisk Thread", *desc, &thread) != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Couldn't register thread with PJLIB.\n");
	}
}

int ast_sip_thread_is_servant(void)
{
	uint32_t *servant_id;

	if (monitor_thread &&
			pthread_self() == *(pthread_t *)pj_thread_get_os_handle(monitor_thread)) {
		return 1;
	}

	servant_id = ast_threadstorage_get(&servant_id_storage, sizeof(*servant_id));
	if (!servant_id) {
		return 0;
	}

	return *servant_id == SIP_SERVANT_ID;
}

void *ast_sip_dict_get(void *ht, const char *key)
{
	unsigned int hval = 0;

	if (!ht) {
		return NULL;
	}

	return pj_hash_get(ht, key, PJ_HASH_KEY_STRING, &hval);
}

void *ast_sip_dict_set(pj_pool_t* pool, void *ht,
		       const char *key, void *val)
{
	if (!ht) {
		ht = pj_hash_create(pool, 11);
	}

	pj_hash_set(pool, ht, key, PJ_HASH_KEY_STRING, 0, val);

	return ht;
}

static pj_bool_t supplement_on_rx_request(pjsip_rx_data *rdata)
{
	struct ast_sip_supplement *supplement;

	if (pjsip_rdata_get_dlg(rdata)) {
		return PJ_FALSE;
	}

	AST_RWLIST_RDLOCK(&supplements);
	AST_LIST_TRAVERSE(&supplements, supplement, next) {
		if (supplement->incoming_request
			&& does_method_match(&rdata->msg_info.msg->line.req.method.name, supplement->method)) {
			struct ast_sip_endpoint *endpoint;

			endpoint = ast_pjsip_rdata_get_endpoint(rdata);
			supplement->incoming_request(endpoint, rdata);
			ao2_cleanup(endpoint);
		}
	}
	AST_RWLIST_UNLOCK(&supplements);

	return PJ_FALSE;
}

static void supplement_outgoing_response(pjsip_tx_data *tdata, struct ast_sip_endpoint *sip_endpoint)
{
	struct ast_sip_supplement *supplement;
	pjsip_cseq_hdr *cseq = pjsip_msg_find_hdr(tdata->msg, PJSIP_H_CSEQ, NULL);
	struct ast_sip_contact *contact = ast_sip_mod_data_get(tdata->mod_data, supplement_module.id, MOD_DATA_CONTACT);

	if (sip_endpoint) {
		ast_sip_message_apply_transport(sip_endpoint->transport, tdata);
	}

	AST_RWLIST_RDLOCK(&supplements);
	AST_LIST_TRAVERSE(&supplements, supplement, next) {
		if (supplement->outgoing_response && does_method_match(&cseq->method.name, supplement->method)) {
			supplement->outgoing_response(sip_endpoint, contact, tdata);
		}
	}
	AST_RWLIST_UNLOCK(&supplements);

	ast_sip_mod_data_set(tdata->pool, tdata->mod_data, supplement_module.id, MOD_DATA_CONTACT, NULL);
	ao2_cleanup(contact);
}

int ast_sip_send_response(pjsip_response_addr *res_addr, pjsip_tx_data *tdata, struct ast_sip_endpoint *sip_endpoint)
{
	pj_status_t status;

	supplement_outgoing_response(tdata, sip_endpoint);
	status = pjsip_endpt_send_response(ast_sip_get_pjsip_endpoint(), res_addr, tdata, NULL, NULL);
	if (status != PJ_SUCCESS) {
		pjsip_tx_data_dec_ref(tdata);
	}

	return status == PJ_SUCCESS ? 0 : -1;
}

int ast_sip_send_stateful_response(pjsip_rx_data *rdata, pjsip_tx_data *tdata, struct ast_sip_endpoint *sip_endpoint)
{
	pjsip_transaction *tsx;

	if (pjsip_tsx_create_uas(NULL, rdata, &tsx) != PJ_SUCCESS) {
		struct ast_sip_contact *contact;

		/* ast_sip_create_response bumps the refcount of the contact and adds it to the tdata.
		 * We'll leak that reference if we don't get rid of it here.
		 */
		contact = ast_sip_mod_data_get(tdata->mod_data, supplement_module.id, MOD_DATA_CONTACT);
		ao2_cleanup(contact);
		ast_sip_mod_data_set(tdata->pool, tdata->mod_data, supplement_module.id, MOD_DATA_CONTACT, NULL);
		pjsip_tx_data_dec_ref(tdata);
		return -1;
	}
	pjsip_tsx_recv_msg(tsx, rdata);

	supplement_outgoing_response(tdata, sip_endpoint);

	if (pjsip_tsx_send_msg(tsx, tdata) != PJ_SUCCESS) {
		pjsip_tx_data_dec_ref(tdata);
		return -1;
	}

	return 0;
}

int ast_sip_create_response(const pjsip_rx_data *rdata, int st_code,
	struct ast_sip_contact *contact, pjsip_tx_data **tdata)
{
	int res = pjsip_endpt_create_response(ast_sip_get_pjsip_endpoint(), rdata, st_code, NULL, tdata);

	if (!res) {
		ast_sip_mod_data_set((*tdata)->pool, (*tdata)->mod_data, supplement_module.id, MOD_DATA_CONTACT, ao2_bump(contact));
	}

	return res;
}

int ast_sip_get_host_ip(int af, pj_sockaddr *addr)
{
	if (af == pj_AF_INET() && !ast_strlen_zero(host_ip_ipv4_string)) {
		pj_sockaddr_copy_addr(addr, &host_ip_ipv4);
		return 0;
	} else if (af == pj_AF_INET6() && !ast_strlen_zero(host_ip_ipv6_string)) {
		pj_sockaddr_copy_addr(addr, &host_ip_ipv6);
		return 0;
	}

	return -1;
}

const char *ast_sip_get_host_ip_string(int af)
{
	if (af == pj_AF_INET()) {
		return host_ip_ipv4_string;
	} else if (af == pj_AF_INET6()) {
		return host_ip_ipv6_string;
	}

	return NULL;
}

int ast_sip_dtmf_to_str(const enum ast_sip_dtmf_mode dtmf,
		        char *buf, size_t buf_len)
{
	switch (dtmf) {
	case AST_SIP_DTMF_NONE:
		ast_copy_string(buf, "none", buf_len);
		break;
	case AST_SIP_DTMF_RFC_4733:
		ast_copy_string(buf, "rfc4733", buf_len);
		break;
	case AST_SIP_DTMF_INBAND:
		ast_copy_string(buf, "inband", buf_len);
		break;
	case AST_SIP_DTMF_INFO:
		ast_copy_string(buf, "info", buf_len);
		break;
	case AST_SIP_DTMF_AUTO:
		ast_copy_string(buf, "auto", buf_len);
		break;
	case AST_SIP_DTMF_AUTO_INFO:
		ast_copy_string(buf, "auto_info", buf_len);
		break;
	default:
		buf[0] = '\0';
		return -1;
	}
	return 0;
}

int ast_sip_str_to_dtmf(const char * dtmf_mode)
{
	int result = -1;

	if (!strcasecmp(dtmf_mode, "info")) {
		result = AST_SIP_DTMF_INFO;
	} else if (!strcasecmp(dtmf_mode, "rfc4733")) {
		result = AST_SIP_DTMF_RFC_4733;
	} else if (!strcasecmp(dtmf_mode, "inband")) {
		result = AST_SIP_DTMF_INBAND;
	} else if (!strcasecmp(dtmf_mode, "none")) {
		result = AST_SIP_DTMF_NONE;
	} else if (!strcasecmp(dtmf_mode, "auto")) {
		result = AST_SIP_DTMF_AUTO;
	} else if (!strcasecmp(dtmf_mode, "auto_info")) {
		result = AST_SIP_DTMF_AUTO_INFO;
	}

	return result;
}

const char *ast_sip_call_codec_pref_to_str(struct ast_flags pref)
{
	const char *value;

	if (ast_sip_call_codec_pref_test(pref, LOCAL) &&  ast_sip_call_codec_pref_test(pref, INTERSECT) && ast_sip_call_codec_pref_test(pref, ALL)) {
		value = "local";
	} else if (ast_sip_call_codec_pref_test(pref, LOCAL) &&  ast_sip_call_codec_pref_test(pref, UNION) && ast_sip_call_codec_pref_test(pref, ALL)) {
		value = "local_merge";
	} else if (ast_sip_call_codec_pref_test(pref, LOCAL) &&  ast_sip_call_codec_pref_test(pref, INTERSECT) && ast_sip_call_codec_pref_test(pref, FIRST)) {
		value = "local_first";
	} else if (ast_sip_call_codec_pref_test(pref, REMOTE) &&  ast_sip_call_codec_pref_test(pref, INTERSECT) && ast_sip_call_codec_pref_test(pref, ALL)) {
		value = "remote";
	} else if (ast_sip_call_codec_pref_test(pref, REMOTE) &&  ast_sip_call_codec_pref_test(pref, UNION) && ast_sip_call_codec_pref_test(pref, ALL)) {
		value = "remote_merge";
	} else if (ast_sip_call_codec_pref_test(pref, REMOTE) &&  ast_sip_call_codec_pref_test(pref, UNION) && ast_sip_call_codec_pref_test(pref, FIRST)) {
		value = "remote_first";
	} else {
		value = "unknown";
	}

	return value;
}

int ast_sip_call_codec_str_to_pref(struct ast_flags *pref, const char *pref_str, int is_outgoing)
{
	pref->flags = 0;

	if (strcmp(pref_str, "local") == 0) {
		ast_set_flag(pref, AST_SIP_CALL_CODEC_PREF_LOCAL | AST_SIP_CALL_CODEC_PREF_INTERSECT | AST_SIP_CALL_CODEC_PREF_ALL);
	} else if (is_outgoing && strcmp(pref_str, "local_merge") == 0) {
		ast_set_flag(pref, AST_SIP_CALL_CODEC_PREF_LOCAL | AST_SIP_CALL_CODEC_PREF_UNION | AST_SIP_CALL_CODEC_PREF_ALL);
	} else if (strcmp(pref_str, "local_first") == 0) {
		ast_set_flag(pref, AST_SIP_CALL_CODEC_PREF_LOCAL | AST_SIP_CALL_CODEC_PREF_INTERSECT | AST_SIP_CALL_CODEC_PREF_FIRST);
	} else if (strcmp(pref_str, "remote") == 0) {
		ast_set_flag(pref, AST_SIP_CALL_CODEC_PREF_REMOTE | AST_SIP_CALL_CODEC_PREF_INTERSECT | AST_SIP_CALL_CODEC_PREF_ALL);
	} else if (is_outgoing && strcmp(pref_str, "remote_merge") == 0) {
		ast_set_flag(pref, AST_SIP_CALL_CODEC_PREF_REMOTE | AST_SIP_CALL_CODEC_PREF_UNION | AST_SIP_CALL_CODEC_PREF_ALL);
	} else if (strcmp(pref_str, "remote_first") == 0) {
		ast_set_flag(pref, AST_SIP_CALL_CODEC_PREF_REMOTE | AST_SIP_CALL_CODEC_PREF_UNION | AST_SIP_CALL_CODEC_PREF_FIRST);
	} else {
		return -1;
	}

	return 0;
}

/*!
 * \brief Set name and number information on an identity header.
 *
 * \param pool Memory pool to use for string duplication
 * \param id_hdr A From, P-Asserted-Identity, or Remote-Party-ID header to modify
 * \param id The identity information to apply to the header
 */
void ast_sip_modify_id_header(pj_pool_t *pool, pjsip_fromto_hdr *id_hdr, const struct ast_party_id *id)
{
	pjsip_name_addr *id_name_addr;
	pjsip_sip_uri *id_uri;

	id_name_addr = (pjsip_name_addr *) id_hdr->uri;
	id_uri = pjsip_uri_get_uri(id_name_addr->uri);

	if (id->name.valid) {
		if (!ast_strlen_zero(id->name.str)) {
			int name_buf_len = strlen(id->name.str) * 2 + 1;
			char *name_buf = ast_alloca(name_buf_len);

			ast_escape_quoted(id->name.str, name_buf, name_buf_len);
			pj_strdup2(pool, &id_name_addr->display, name_buf);
		} else {
			pj_strdup2(pool, &id_name_addr->display, NULL);
		}
	}

	if (id->number.valid) {
		pj_strdup2(pool, &id_uri->user, id->number.str);
	}
}


static void remove_request_headers(pjsip_endpoint *endpt)
{
	const pjsip_hdr *request_headers = pjsip_endpt_get_request_headers(endpt);
	pjsip_hdr *iter = request_headers->next;

	while (iter != request_headers) {
		pjsip_hdr *to_erase = iter;
		iter = iter->next;
		pj_list_erase(to_erase);
	}
}

long ast_sip_threadpool_queue_size(void)
{
	return ast_threadpool_queue_size(sip_threadpool);
}

struct ast_threadpool *ast_sip_threadpool(void)
{
	return sip_threadpool;
}

#ifdef TEST_FRAMEWORK
AST_TEST_DEFINE(xml_sanitization_end_null)
{
	char sanitized[8];

	switch (cmd) {
	case TEST_INIT:
		info->name = "xml_sanitization_end_null";
		info->category = "/res/res_pjsip/";
		info->summary = "Ensure XML sanitization works as expected with a long string";
		info->description = "This test sanitizes a string which exceeds the output\n"
			"buffer size. Once done the string is confirmed to be NULL terminated.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_sip_sanitize_xml("aaaaaaaaaaaa", sanitized, sizeof(sanitized));
	if (sanitized[7] != '\0') {
		ast_test_status_update(test, "Sanitized XML string is not null-terminated when it should be\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(xml_sanitization_exceeds_buffer)
{
	char sanitized[8];

	switch (cmd) {
	case TEST_INIT:
		info->name = "xml_sanitization_exceeds_buffer";
		info->category = "/res/res_pjsip/";
		info->summary = "Ensure XML sanitization does not exceed buffer when output won't fit";
		info->description = "This test sanitizes a string which before sanitization would\n"
			"fit within the output buffer. After sanitization, however, the string would\n"
			"exceed the buffer. Once done the string is confirmed to be NULL terminated.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	ast_sip_sanitize_xml("<><><>&", sanitized, sizeof(sanitized));
	if (sanitized[7] != '\0') {
		ast_test_status_update(test, "Sanitized XML string is not null-terminated when it should be\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}
#endif

/*!
 * \internal
 * \brief Reload configuration within a PJSIP thread
 */
static int reload_configuration_task(void *obj)
{
	ast_res_pjsip_reload_configuration();
	ast_res_pjsip_init_options_handling(1);
	ast_sip_initialize_dns();
	return 0;
}

static int unload_pjsip(void *data)
{
	/*
	 * These calls need the pjsip endpoint and serializer to clean up.
	 * If they're not set, then there's nothing to clean up anyway.
	 */
	if (ast_pjsip_endpoint && sip_serializer_pool) {
		ast_res_pjsip_cleanup_options_handling();
		ast_res_pjsip_cleanup_message_filter();
		ast_sip_destroy_distributor();
		ast_sip_destroy_transport_management();
		ast_res_pjsip_destroy_configuration();
		ast_sip_destroy_system();
		ast_sip_destroy_global_headers();
		ast_sip_unregister_service(&supplement_module);
		ast_sip_destroy_transport_events();
	}

	if (monitor_thread) {
		stop_monitor_thread();
		monitor_thread = NULL;
	}

	if (memory_pool) {
		/* This mimics the behavior of pj_pool_safe_release
		 * which was introduced in pjproject 2.6.
		 */
		pj_pool_t *temp_pool = memory_pool;

		memory_pool = NULL;
		pj_pool_release(temp_pool);
	}

	ast_pjsip_endpoint = NULL;

	if (caching_pool.lock) {
		ast_pjproject_caching_pool_destroy(&caching_pool);
	}

	pj_shutdown();

	return 0;
}

static int load_pjsip(void)
{
	const unsigned int flags = 0; /* no port, no brackets */
	pj_status_t status;

	/* The third parameter is just copied from
	 * example code from PJLIB. This can be adjusted
	 * if necessary.
	 */
	ast_pjproject_caching_pool_init(&caching_pool, NULL, 1024 * 1024);
	if (pjsip_endpt_create(&caching_pool.factory, "SIP", &ast_pjsip_endpoint) != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Failed to create PJSIP endpoint structure. Aborting load\n");
		goto error;
	}

	/* PJSIP will automatically try to add a Max-Forwards header. Since we want to control that,
	 * we need to stop PJSIP from doing it automatically
	 */
	remove_request_headers(ast_pjsip_endpoint);

	memory_pool = pj_pool_create(&caching_pool.factory, "SIP", 1024, 1024, NULL);
	if (!memory_pool) {
		ast_log(LOG_ERROR, "Failed to create memory pool for SIP. Aborting load\n");
		goto error;
	}

	if (!pj_gethostip(pj_AF_INET(), &host_ip_ipv4)) {
		pj_sockaddr_print(&host_ip_ipv4, host_ip_ipv4_string, sizeof(host_ip_ipv4_string), flags);
		ast_verb(3, "Local IPv4 address determined to be: %s\n", host_ip_ipv4_string);
	}

	if (!pj_gethostip(pj_AF_INET6(), &host_ip_ipv6)) {
		pj_sockaddr_print(&host_ip_ipv6, host_ip_ipv6_string, sizeof(host_ip_ipv6_string), flags);
		ast_verb(3, "Local IPv6 address determined to be: %s\n", host_ip_ipv6_string);
	}

	pjsip_tsx_layer_init_module(ast_pjsip_endpoint);
	pjsip_ua_init_module(ast_pjsip_endpoint, NULL);

	monitor_continue = 1;
	status = pj_thread_create(memory_pool, "SIP", (pj_thread_proc *) &monitor_thread_exec,
			NULL, PJ_THREAD_DEFAULT_STACK_SIZE * 2, 0, &monitor_thread);
	if (status != PJ_SUCCESS) {
		ast_log(LOG_ERROR, "Failed to start SIP monitor thread. Aborting load\n");
		goto error;
	}

	return AST_MODULE_LOAD_SUCCESS;

error:
	return AST_MODULE_LOAD_DECLINE;
}

/*
 * This is a place holder function to ensure that pjmedia_strerr() is at
 * least directly referenced by this module to ensure that the loader
 * linker will link to the function.  If a module only indirectly
 * references a function from another module, such as a callback parameter
 * to a function, the loader linker has been known to miss the link.
 */
void never_called_res_pjsip(void);
void never_called_res_pjsip(void)
{
	pjmedia_strerror(0, NULL, 0);
}

static int load_module(void)
{
	struct ast_threadpool_options options;

	/* pjproject and config_system need to be initialized before all else */
	if (pj_init() != PJ_SUCCESS) {
		return AST_MODULE_LOAD_DECLINE;
	}

	if (pjlib_util_init() != PJ_SUCCESS) {
		goto error;
	}

	/* Register PJMEDIA error codes for SDP parsing errors */
	if (pj_register_strerror(PJMEDIA_ERRNO_START, PJ_ERRNO_SPACE_SIZE, pjmedia_strerror)
		!= PJ_SUCCESS) {
		ast_log(LOG_WARNING, "Failed to register pjmedia error codes.  Codes will not be decoded.\n");
	}

	if (ast_sip_initialize_system()) {
		ast_log(LOG_ERROR, "Failed to initialize SIP 'system' configuration section. Aborting load\n");
		goto error;
	}

	/* The serializer needs threadpool and threadpool needs pjproject to be initialized so it's next */
	sip_get_threadpool_options(&options);
	options.thread_start = sip_thread_start;
	sip_threadpool = ast_threadpool_create("pjsip", NULL, &options);
	if (!sip_threadpool) {
		goto error;
	}

	sip_serializer_pool = ast_serializer_pool_create(
		"pjsip/default", SERIALIZER_POOL_SIZE, sip_threadpool, -1);
	if (!sip_serializer_pool) {
		ast_log(LOG_ERROR, "Failed to create SIP serializer pool. Aborting load\n");
		goto error;
	}

	if (ast_sip_initialize_scheduler()) {
		ast_log(LOG_ERROR, "Failed to start scheduler. Aborting load\n");
		goto error;
	}

	/* Now load all the pjproject infrastructure. */
	if (load_pjsip()) {
		goto error;
	}

	if (ast_sip_initialize_transport_events()) {
		ast_log(LOG_ERROR, "Failed to initialize SIP transport monitor. Aborting load\n");
		goto error;
	}

	ast_sip_initialize_dns();
	ast_sip_initialize_global_headers();

	if (ast_res_pjsip_preinit_options_handling()) {
		ast_log(LOG_ERROR, "Failed to pre-initialize OPTIONS handling. Aborting load\n");
		goto error;
	}

	if (ast_res_pjsip_initialize_configuration()) {
		ast_log(LOG_ERROR, "Failed to initialize SIP configuration. Aborting load\n");
		goto error;
	}

	ast_sip_initialize_resolver();
	ast_sip_initialize_dns();

	if (ast_sip_initialize_transport_management()) {
		ast_log(LOG_ERROR, "Failed to initialize SIP transport management. Aborting load\n");
		goto error;
	}

	if (ast_sip_initialize_distributor()) {
		ast_log(LOG_ERROR, "Failed to register distributor module. Aborting load\n");
		goto error;
	}

	if (ast_sip_register_service(&supplement_module)) {
		ast_log(LOG_ERROR, "Failed to initialize supplement hooks. Aborting load\n");
		goto error;
	}

	if (ast_res_pjsip_init_options_handling(0)) {
		ast_log(LOG_ERROR, "Failed to initialize OPTIONS handling. Aborting load\n");
		goto error;
	}

	if (ast_res_pjsip_init_message_filter()) {
		ast_log(LOG_ERROR, "Failed to initialize message IP updating. Aborting load\n");
		goto error;
	}

	ast_cli_register_multiple(cli_commands, ARRAY_LEN(cli_commands));

	AST_TEST_REGISTER(xml_sanitization_end_null);
	AST_TEST_REGISTER(xml_sanitization_exceeds_buffer);

	return AST_MODULE_LOAD_SUCCESS;

error:
	unload_pjsip(NULL);

	/* These functions all check for NULLs and are safe to call at any time */
	ast_sip_destroy_scheduler();
	ast_serializer_pool_destroy(sip_serializer_pool);
	ast_threadpool_shutdown(sip_threadpool);

	return AST_MODULE_LOAD_DECLINE;
}

static int reload_module(void)
{
	/*
	 * We must wait for the reload to complete so multiple
	 * reloads cannot happen at the same time.
	 */
	if (ast_sip_push_task_wait_servant(NULL, reload_configuration_task, NULL)) {
		ast_log(LOG_WARNING, "Failed to reload PJSIP\n");
		return -1;
	}

	return 0;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(xml_sanitization_end_null);
	AST_TEST_UNREGISTER(xml_sanitization_exceeds_buffer);
	ast_cli_unregister_multiple(cli_commands, ARRAY_LEN(cli_commands));

	/* The thread this is called from cannot call PJSIP/PJLIB functions,
	 * so we have to push the work to the threadpool to handle
	 */
	ast_sip_push_task_wait_servant(NULL, unload_pjsip, NULL);
	ast_sip_destroy_scheduler();
	ast_serializer_pool_destroy(sip_serializer_pool);
	ast_threadpool_shutdown(sip_threadpool);

	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_GLOBAL_SYMBOLS | AST_MODFLAG_LOAD_ORDER, "Basic SIP resource",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_CHANNEL_DEPEND - 5,
	.requires = "dnsmgr,res_pjproject,res_sorcery_config,res_sorcery_memory,res_sorcery_astdb",
	.optional_modules = "res_statsd",
);
