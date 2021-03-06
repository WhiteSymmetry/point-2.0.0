/*
 * configuration.cc
 *
 *  Created on: 17 Apr 2016
 *      Author: Sebastian Robitzsch <sebastian.robitzsch@interdigital.com>
 *
 * This file is part of Blackadder.
 *
 * Blackadder is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * Blackadder is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * Blackadder.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "configuration.hh"

using namespace configuration;
using namespace log4cxx;

LoggerPtr Configuration::logger(Logger::getLogger("configuration"));

Configuration::Configuration()
{
	_bufferCleanerInterval = 10;// seconds
	_cNap = true;
	_hostBasedNap = false;
	_httpHandler = true;
	_httpProxyPort = 3127; // port
	_icnGateway = false;
	_ltpInitialCredit = 10; // segments, not bytes
	_ltpRttListSize = 10; // Default
	_ltpRttMultiplier = 2;
	_mitu = 1300; // octets
	_molyInterval = 0;// 0 = disabled
	_mtu = 1500; // octets
	_socketType = RAWIP;
	_surrogacy = false;
	_tcDropRate = -1; // packets
	_tcpClientSocketBufferSize = 65535;
	_tcpServerSocketBufferSize = 65535;
}

void Configuration::addFqdn(IcnId &cid, IpAddress &ipAddress, uint16_t port)
{
	list<pair<IcnId, pair<IpAddress, uint16_t>>>::iterator fqdnsIt;
	_fqdnsMutex.lock();
	fqdnsIt = _fqdns.begin();
	while (fqdnsIt != _fqdns.end())
	{
		if (fqdnsIt->first.uint() == cid.uint())
		{
			if (fqdnsIt->second.first.uint() == ipAddress.uint())
			{
				LOG4CXX_DEBUG(logger, "CID " << cid.print() << " and IP address"
						" " << ipAddress.str() << " already exists in list of "
								"known FQDNs and their IP addresses");
				_fqdnsMutex.unlock();
				return;
			}
			// same FQDN but different IP
			fqdnsIt->second.first = ipAddress;
			LOG4CXX_TRACE(logger, "IP address for CID " << cid.print()
					<< " has been changed to " << ipAddress.str());
			_fqdnsMutex.unlock();
			return;
		}
		fqdnsIt++;
	}
	// FQDN has not been found. Add it
	pair<IpAddress, uint16_t> serverConfig;
	serverConfig.first = ipAddress;
	serverConfig.second = port;
	_fqdns.push_back(pair<IcnId, pair<IpAddress, uint16_t>>(cid, serverConfig));
	LOG4CXX_TRACE(logger, "CID (FQDN) " << cid.print() << " and its endpoint "
			"serving it on IP " << ipAddress.str() << ":" << port << " has been"
			" added to list of known FQDNs");
	_fqdnsMutex.unlock();
}

uint32_t Configuration::bufferCleanerInterval()
{
	return _bufferCleanerInterval;
}

bool Configuration::cNap()
{
	return _cNap;
}

void Configuration::deleteFqdn(IcnId &cid)
{
	list<pair<IcnId, pair<IpAddress, uint16_t>>>::iterator fqdnsIt;
	_fqdnsMutex.lock();
	fqdnsIt = _fqdns.begin();
	while (fqdnsIt != _fqdns.end())
	{
		if (fqdnsIt->first.uint() == cid.uint())
		{
			LOG4CXX_TRACE(logger, "CID " << cid.print() << " deleted from list "
					"of list known FQDN");
			_fqdnsMutex.unlock();
			return;
		}
		fqdnsIt++;
	}
	_fqdnsMutex.unlock();
}

bool Configuration::icnGateway()
{
	return _icnGateway;
}

RoutingPrefix Configuration::icnGatewayRoutingPrefix()
{
	return _icnGatewayRoutingPrefix;
}
uint32_t Configuration::icnHeaderLength()
{
	return 20;//FIXME obtain the exact number (through MAPI maybe?)
}

IpAddress Configuration::endpointIpAddress()
{
	return _endpointIpAddress;
}

list<pair<IcnId, pair<IpAddress, uint16_t>>> Configuration::fqdns()
{
	list<pair<IcnId, pair<IpAddress, uint16_t>>> fqdns;
	_fqdnsMutex.lock();
	fqdns = _fqdns;
	_fqdnsMutex.unlock();
	return fqdns;
}

bool Configuration::hostBasedNap()
{
	return _hostBasedNap;
}

RoutingPrefix Configuration::hostRoutingPrefix()
{
	return _hostRoutingPrefix;
}

map<uint32_t, RoutingPrefix> *Configuration::hostRoutingPrefixes()
{
	return &_routingPrefixes;
}

bool Configuration::httpHandler()
{
	return _httpHandler;
}

uint16_t Configuration::httpProxyPort()
{
	return _httpProxyPort;
}

uint16_t Configuration::ltpInitialCredit()
{
	return _ltpInitialCredit;
}

uint32_t Configuration::ltpRttListSize()
{
	return _ltpRttListSize;
}

uint16_t Configuration::mitu()
{
	return (uint16_t)_mitu;
}

uint32_t Configuration::molyInterval()
{
	return _molyInterval;
}

uint16_t Configuration::mtu()
{
	return (uint16_t)_mtu;
}

string Configuration::networkDevice()
{
	return _networkDevice;
}

NodeId &Configuration::nodeId()
{
	return _nodeId;
}

bool Configuration::parse(string file)
{
	Config cfg;
	try
	{
		cfg.readFile(file.c_str());
	}
	catch(const FileIOException &fioex)
	{
		LOG4CXX_FATAL(logger, "Cannot read " << file);
		return false;
	}
	catch(const ParseException &pex)
	{
		LOG4CXX_FATAL(logger, "Parsing error in file " << file);
		return false;
	}
	// Reading root
	const Setting& root = cfg.getRoot();
	try
	{
		string ip, mask;
		uint32_t nodeId;
		const Setting &napConfig = root["napConfig"];
		// Interface
		if (!napConfig.lookupValue("interface", _networkDevice))
		{
			LOG4CXX_FATAL(logger, "'interface' is missing in " << file);
			return false;
		}
		// MTU
		int sock;
		struct ifreq ifr;
		sock = socket(AF_INET, SOCK_DGRAM, 0);
		ifr.ifr_addr.sa_family = AF_INET;
		strncpy(ifr.ifr_name, _networkDevice.c_str(), IFNAMSIZ-1);
		if(ioctl(sock, SIOCGIFMTU, &ifr) == -1)
		{
			LOG4CXX_WARN(logger, "MTU could not be read for interface "
					<< _networkDevice);
		}
		else
		{
			_mtu = ifr.ifr_mtu;
			LOG4CXX_TRACE(logger, "MTU of " << _mtu << " read for network "
					"device");
		}
		close(sock);
		// MITU configured by user
		if (napConfig.lookupValue("mitu", _mitu))
		{
			if (_mitu > 1300)
			{
				LOG4CXX_WARN(logger, "At the moment the NAP can only guess the "
						"exact maximum MITU. It is recommended to not increase "
						"it above 1300 octets");
			}
			// make it a multiple of 8
			if ((_mitu % 8) != 0)
			{
				LOG4CXX_DEBUG(logger, "Making given MITU of " << _mitu << " a "
						"multiple of 8");
				_mitu = _mitu - (_mitu % 8);
			}

			LOG4CXX_TRACE(logger, "MITU set to " << _mitu);
		}
		// Routing prefix
		if (!napConfig.lookupValue("networkAddress", ip))
		{
			LOG4CXX_FATAL(logger, "'networkAddress' is missing in " << file);
			return false;
		}
		if(!napConfig.lookupValue("netmask", mask))
		{
			LOG4CXX_FATAL(logger, "'netmask' is missing in " << file);
			return false;
		}
		_hostRoutingPrefix.networkAddress(ip);
		_hostRoutingPrefix.netmask(mask);
		// I'm the ICN GW
		if (mask.compare("0.0.0.0") == 0)
		{
			_icnGateway = true;
			string networkAddress;
			string netmask;
			if (!napConfig.lookupValue("icnGwNetworkAddress", networkAddress))
			{
				LOG4CXX_WARN(logger, "Please provide the network address of the"
						" ICN GW so that the NAP does not get too overloaded "
						"with dropping packets unneccessarily");
			}
			_icnGatewayRoutingPrefix.networkAddress(networkAddress);
			LOG4CXX_TRACE(logger, "ICN GW network address set to "
					<< _icnGatewayRoutingPrefix.networkAddress().str());
			if (!napConfig.lookupValue("icnGwNetmask", netmask))
			{
				LOG4CXX_WARN(logger, "Please provide the netmask of the ICN GW "
						"so that the NAP does not get too overloaded "
						"with dropping packets unneccessarily");
				_icnGatewayRoutingPrefix.networkAddress(string());
			}
			_icnGatewayRoutingPrefix.netmask(netmask);
			LOG4CXX_TRACE(logger, "ICN GW netmask set to "
					<< _icnGatewayRoutingPrefix.netmask().str());
		}
		// Host-based deployment
		if(napConfig.lookupValue("ipEndpoint", ip))
		{
			_hostBasedNap = true;
			_endpointIpAddress = ip;
		}
		//Node ID
		if (!napConfig.lookupValue("nodeId", nodeId))
		{
			LOG4CXX_FATAL(logger, "'nodeId' is missing in " << file);
			return false;
		}
		if (nodeId > 99999999)
		{
			LOG4CXX_FATAL(logger, "NID cannot be larger than eight digits");
			return false;
		}
		_nodeId = nodeId;
		LOG4CXX_TRACE(logger, "NID set to " << _nodeId.uint());
		// LTP Credit
		if (napConfig.lookupValue("ltpInitialCredit", _ltpInitialCredit))
		{
			LOG4CXX_TRACE(logger, "Initial credit for LTP sessions set to "
					<< _ltpInitialCredit << " segments");
		}
		if (napConfig.lookupValue("httpHandler", _httpHandler))
		{
			if (!_httpHandler)
			{
				LOG4CXX_TRACE(logger, "HTTP handler deactivated");
			}
			else
			{
				LOG4CXX_TRACE(logger, "HTTP handler stays activated");
			}
		}
		// HTTP proxy port
		if (_httpHandler && napConfig.lookupValue("httpProxyPort",
				_httpProxyPort))
		{
			LOG4CXX_TRACE(logger, "HTTP proxy port set to " << _httpProxyPort);
		}
		// TCP socket buffer sizes
		if (napConfig.lookupValue("tcpClientSocketBufferSize",
				_tcpClientSocketBufferSize))
		{
			if (_tcpClientSocketBufferSize > 65535)
			{
				_tcpClientSocketBufferSize = 65535;
				LOG4CXX_INFO(logger, "TCP client socket buffer sizes larger "
						"then 65535 not supported");
			}
			LOG4CXX_TRACE(logger, "TCP client socket buffer set to "
					<< _tcpClientSocketBufferSize);
		}
		if (_httpHandler && napConfig.lookupValue("tcpServerSocketBufferSize",
				_tcpServerSocketBufferSize))
		{
			if (_tcpServerSocketBufferSize > 65535)
			{
				_tcpServerSocketBufferSize = 65535;
				LOG4CXX_INFO(logger, "TCP client socket buffer sizes larger "
						"then 65535 not supported");
			}
			//FIXME if WU/WUD is properly implemented in cNAP this goes away
			if ((uint32_t)(ltpInitialCredit() * mitu())
					< _tcpServerSocketBufferSize)
			{
				LOG4CXX_WARN(logger, "TCP server socket buffer size too large! "
						"Lower it by " << _tcpServerSocketBufferSize -
						(ltpInitialCredit() * mitu()) << " bytes");
			}
			LOG4CXX_TRACE(logger, "TCP server socket buffer set to "
					<< _tcpServerSocketBufferSize);
		}
		else if (_httpHandler)
		{//FIXME if WU/WUD is properly implemented in cNAP this goes away
			if ((uint32_t)(ltpInitialCredit() * mitu())
					< _tcpServerSocketBufferSize)
			{
				_tcpServerSocketBufferSize = ltpInitialCredit() * mitu();
				LOG4CXX_DEBUG(logger, "TCP server socket buffer size "
						"automatically adjusted to "
						<< _tcpServerSocketBufferSize << " bytes to prevent LTP"
								" flow control operations in the cNAP")
			}
		}
		// Buffer cleaner interval
		if (napConfig.lookupValue("bufferCleanerInterval",
				_bufferCleanerInterval))
		{
			LOG4CXX_TRACE(logger, "Buffer cleaner interval set to "
					<< _bufferCleanerInterval << "s");
		}
		else
		{
			LOG4CXX_DEBUG(logger, "Buffer cleaner interval was not provided. "
					"Using default value of " << _bufferCleanerInterval << "s");
		}
		// LTP timeout multiplier
		if (napConfig.lookupValue("ltpRttMultiplier", _ltpRttMultiplier))
		{
			LOG4CXX_TRACE(logger, "LTP multiplier for RTT-based timeout counter"
					" set to " << _ltpRttMultiplier);
		}
		// LTP RTT list size
		if (napConfig.lookupValue("ltpRttListSize", _ltpRttListSize))
		{
			if (_ltpRttListSize < 1)
			{
				LOG4CXX_WARN(logger, "'ltpRttListSize' cannot be smaller than "
						"1");
			}
			else
			{
				LOG4CXX_TRACE(logger, "LTP RTT list size set to "
						<< _ltpRttListSize);
			}
		}
		// Surrogacy
		if (_httpHandler && napConfig.lookupValue("surrogacy", _surrogacy))
		{
			if (_surrogacy)
			{
				_cNap = false;
				LOG4CXX_TRACE(logger, "eNAP functionality enabled");
			}
			else
			{
				LOG4CXX_TRACE(logger, "eNAP functionality disabled");
			}
		}
		// Traffic control - Drop rate
		if (napConfig.lookupValue("tcDropRate", _tcDropRate))
		{
			if (_tcDropRate == 0)
			{
				LOG4CXX_FATAL(logger, "TC drop rate cannot be 0, dude!");
				return false;
			}
			LOG4CXX_TRACE(logger, "Traffic control drop rate set to "
					<< (float)1 / _tcDropRate);
		}
		// MOLY interface
		if (napConfig.lookupValue("molyInterval", _molyInterval))
		{
			LOG4CXX_TRACE(logger, "MOLY reporting interval set to "
					<< _molyInterval);
		}
		// Socket type
		string socketType;
		if (napConfig.lookupValue("socketType", socketType))
		{
			if (socketType.compare("libnet") == 0)
			{
				_socketType = LIBNET;
				LOG4CXX_TRACE(logger, "Socket type is LIBNET");
			}
			else if (socketType.compare("linux") == 0)
			{
				_socketType = RAWIP;
				LOG4CXX_TRACE(logger, "Socket type is RAWIP");
			}
			else
			{
				LOG4CXX_FATAL(logger,"Unknown socket type");
			}
		}
		else
		{
			_socketType = RAWIP;
			LOG4CXX_TRACE(logger, "Socket type is RAWIP");
		}

	}
	catch(const SettingNotFoundException &nfex)
	{
			LOG4CXX_FATAL(logger, "A mandatory root setting is missing in "
					<< file);
			return false;
	}
	// Reading routing prefixes
	const Setting& routingPrefixes = root["napConfig"]["routingPrefixes"];
	size_t count = routingPrefixes.getLength();
	for (size_t i = 0; i < count; i++)
	{
		string ip, mask;
		const Setting &routingPrefix = routingPrefixes[i];
		if (!(routingPrefix.lookupValue("networkAddress", ip)))
		{
			LOG4CXX_FATAL(logger, "Network address of a routing prefix could"
					"not be read in " << file);
			return false;
		}
		if (!(routingPrefix.lookupValue("netmask", mask)))
		{
			LOG4CXX_FATAL(logger, "Netmask of a routing prefix could not be"
					"read in " << file);
			return false;
		}
		// adding prefix to map
		RoutingPrefix prefix(ip, mask);
		_routingPrefixes.insert(pair<uint32_t, RoutingPrefix>(prefix.uint(),
				prefix));
		LOG4CXX_TRACE(logger, "Routing prefix " << prefix.str() << " added");
	}
	// Reading FQDN Registrations
	if (_httpHandler)
	{
		const Setting &fqdns = root["napConfig"]["fqdns"];
		string id, prefixId;
		count = fqdns.getLength();
		for (size_t i = 0; i < count; i++)
		{
			string ip, fqdn;
			int port;
			const Setting &f = fqdns[i];
			// FQDN
			if (!(f.lookupValue("fqdn", fqdn)))
			{
				LOG4CXX_FATAL(logger, "FQDN could not be read");
				return false;
			}
			// IP address
			if (!(f.lookupValue("ipAddress", ip)))
			{
				LOG4CXX_FATAL(logger, "IP address for FQDN registration"
						<< " could not be read");
				return false;
			}
			// Port
			if (!(f.lookupValue("port", port)))
			{
				port = 80;
			}
			_cNap = false;
			IpAddress ipAddress(ip);
			pair<IpAddress, uint16_t> serverConfig;
			serverConfig.first = ipAddress;
			serverConfig.second = port;
			_fqdns.push_front(pair<string, pair<IpAddress, uint16_t>>(fqdn,
					serverConfig));
			LOG4CXX_TRACE(logger, "FQDN " << fqdn << " and its IP address "
					<< ipAddress.str() << " + Port " << port << " added");
		}
	}
	return true;
}

uint16_t Configuration::ltpRttMultiplier()
{
	return _ltpRttMultiplier;
}

SocketType Configuration::socketType()
{
	return _socketType;
}

bool Configuration::surrogacy()
{
	return _surrogacy;
}

int Configuration::tcDropRate()
{
	return _tcDropRate;
}

uint16_t Configuration::tcpClientSocketBufferSize()
{
	return _tcpClientSocketBufferSize;
}

uint16_t Configuration::tcpServerSocketBufferSize()
{
	return _tcpServerSocketBufferSize;
}
