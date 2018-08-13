/******************************************************************************
 * Copyright © 2014-2018 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

#include "CCinclude.h"

/*
 FinalizeCCTx is a very useful function that will properly sign both CC and normal inputs, adds normal change and the opreturn.
 
 This allows the contract transaction functions to create the appropriate vins and vouts and have FinalizeCCTx create a properly signed transaction.
 
 By using -addressindex=1, it allows tracking of all the CC addresses
 */
 
bool SignTx(CMutableTransaction &mtx,int32_t vini,uint64_t utxovalue,const CScript scriptPubKey)
{
#ifdef ENABLE_WALLET
    CTransaction txNewConst(mtx); SignatureData sigdata; const CKeyStore& keystore = *pwalletMain;
    auto consensusBranchId = CurrentEpochBranchId(chainActive.Height() + 1, Params().GetConsensus());
    if ( ProduceSignature(TransactionSignatureCreator(&keystore,&txNewConst,vini,utxovalue,SIGHASH_ALL),scriptPubKey,sigdata,consensusBranchId) != 0 )
    {
        UpdateTransaction(mtx,vini,sigdata);
        return(true);
    } else fprintf(stderr,"signing error for SignTx vini.%d %.8f\n",vini,(double)utxovalue/COIN);
#else
    return(false);
#endif
}

std::string FinalizeCCTx(uint64_t CCmask,struct CCcontract_info *cp,CMutableTransaction &mtx,CPubKey mypk,uint64_t txfee,CScript opret)
{
    auto consensusBranchId = CurrentEpochBranchId(chainActive.Height() + 1, Params().GetConsensus());
    CTransaction vintx; std::string hex; uint256 hashBlock; uint64_t mask=0,nmask=0,vinimask=0,utxovalues[64],change,normalinputs=0,totaloutputs=0,normaloutputs=0,totalinputs=0; int32_t i,utxovout,n,err = 0; char myaddr[64],destaddr[64],unspendable[64]; uint8_t *privkey,myprivkey[32],unspendablepriv[32],*msg32 = 0; CC *mycond=0,*othercond=0,*cond; CPubKey unspendablepk;
    n = mtx.vout.size();
    for (i=0; i<n; i++)
    {
        if ( mtx.vout[i].scriptPubKey.IsPayToCryptoCondition() == 0 )
            normaloutputs += mtx.vout[i].nValue;
        totaloutputs += mtx.vout[i].nValue;
    }
    if ( (n= mtx.vin.size()) > 64 )
    {
        fprintf(stderr,"FinalizeCCTx: %d is too many vins\n",n);
        return("0");
    }
    Myprivkey(myprivkey);
    unspendablepk = GetUnspendable(cp,unspendablepriv);
    GetCCaddress(cp,myaddr,mypk);
    mycond = MakeCCcond1(cp->evalcode,mypk);
    GetCCaddress(cp,unspendable,unspendablepk);
    othercond = MakeCCcond1(cp->evalcode,unspendablepk);
    //fprintf(stderr,"myCCaddr.(%s) %p vs unspendable.(%s) %p\n",myaddr,mycond,unspendable,othercond);
    memset(utxovalues,0,sizeof(utxovalues));
    for (i=0; i<n; i++)
    {
        if ( GetTransaction(mtx.vin[i].prevout.hash,vintx,hashBlock,false) != 0 )
        {
            utxovout = mtx.vin[i].prevout.n;
            utxovalues[i] = vintx.vout[utxovout].nValue;
            totalinputs += utxovalues[i];
            if ( vintx.vout[utxovout].scriptPubKey.IsPayToCryptoCondition() == 0 )
            {
                //fprintf(stderr,"vin.%d is normal %.8f\n",i,(double)utxovalues[i]/COIN);
                normalinputs += utxovalues[i];
                vinimask |= (1LL << i);
            }
            else
            {
                mask |= (1LL << i);
            }
        } else fprintf(stderr,"FinalizeCCTx couldnt find %s\n",mtx.vin[i].prevout.hash.ToString().c_str());
    }
    nmask = (1LL << n) - 1;
    if ( 0 && (mask & nmask) != (CCmask & nmask) )
        fprintf(stderr,"mask.%llx vs CCmask.%llx %llx %llx %llx\n",(long long)(mask & nmask),(long long)(CCmask & nmask),(long long)mask,(long long)CCmask,(long long)nmask);
    if ( totalinputs >= totaloutputs+2*txfee )
    {
        change = totalinputs - (totaloutputs+txfee);
        mtx.vout.push_back(CTxOut(change,CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG));
    }
    if ( opret.size() > 0 )
        mtx.vout.push_back(CTxOut(0,opret));
    PrecomputedTransactionData txdata(mtx);
    n = mtx.vin.size();
    for (i=0; i<n; i++)
    {
        if ( GetTransaction(mtx.vin[i].prevout.hash,vintx,hashBlock,false) != 0 )
        {
            utxovout = mtx.vin[i].prevout.n;
            if ( vintx.vout[utxovout].scriptPubKey.IsPayToCryptoCondition() == 0 )
            {
                if ( SignTx(mtx,i,vintx.vout[utxovout].nValue,vintx.vout[utxovout].scriptPubKey) == 0 )
                    fprintf(stderr,"signing error for vini.%d of %llx\n",i,(long long)vinimask);
            }
            else
            {
                Getscriptaddress(destaddr,vintx.vout[utxovout].scriptPubKey);
                //fprintf(stderr,"vin.%d is CC %.8f -> (%s)\n",i,(double)utxovalues[i]/COIN,destaddr);
                if ( strcmp(destaddr,myaddr) == 0 )
                {
                    privkey = myprivkey;
                    cond = mycond;
                    //fprintf(stderr,"my CC addr.(%s)\n",myaddr);
                }
                else if ( strcmp(destaddr,unspendable) == 0 )
                {
                    privkey = unspendablepriv;
                    cond = othercond;
                    //fprintf(stderr,"unspendable CC addr.(%s)\n",unspendable);
                }
                else
                {
                    fprintf(stderr,"vini.%d has unknown CC address.(%s)\n",i,destaddr);
                    continue;
                }
                uint256 sighash = SignatureHash(CCPubKey(cond), mtx, i, SIGHASH_ALL, utxovalues[i],consensusBranchId, &txdata);
                if ( cc_signTreeSecp256k1Msg32(cond,privkey,sighash.begin()) != 0 )
                {
                    //int32_t z;
                    //for (z=0; z<32; z++)
                    //    fprintf(stderr,"%02x",((uint8_t *)sighash.begin())[z]);
                    //fprintf(stderr," sighash, ");
                    //for (z=0; z<32; z++)
                    //   fprintf(stderr,"%02x",privkey[z]);
                    //fprintf(stderr," signed with privkey\n");
                    mtx.vin[i].scriptSig = CCSig(cond);
                }
                else fprintf(stderr,"vini.%d has CC signing error address.(%s)\n",i,destaddr);
            }
        } else fprintf(stderr,"FinalizeCCTx couldnt find %s\n",mtx.vin[i].prevout.hash.ToString().c_str());
    }
    if ( mycond != 0 )
        cc_free(mycond);
    if ( othercond != 0 )
        cc_free(othercond);
    std::string strHex = EncodeHexTx(mtx);
    if ( strHex.size() > 0 )
        return(strHex);
    else return("0");
}

void SetCCunspents(std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &unspentOutputs,char *coinaddr)
{
    int32_t type=0,i,n; char *ptr; std::string addrstr; uint160 hashBytes; std::vector<std::pair<uint160, int> > addresses;
    n = (int32_t)strlen(coinaddr);
    addrstr.resize(n+1);
    ptr = (char *)addrstr.data();
    for (i=0; i<=n; i++)
        ptr[i] = coinaddr[i];
    CBitcoinAddress address(addrstr);
    if ( address.GetIndexKey(hashBytes, type) == 0 )
        return;
    addresses.push_back(std::make_pair(hashBytes,type));
    for (std::vector<std::pair<uint160, int> >::iterator it = addresses.begin(); it != addresses.end(); it++)
    {
        if ( GetAddressUnspent((*it).first, (*it).second, unspentOutputs) == 0 )
            return;
    }
}

void SetCCtxids(std::vector<std::pair<CAddressIndexKey, CAmount> > &addressIndex,char *coinaddr)
{
    int32_t type=0,i,n; char *ptr; std::string addrstr; uint160 hashBytes; std::vector<std::pair<uint160, int> > addresses;
    n = (int32_t)strlen(coinaddr);
    addrstr.resize(n+1);
    ptr = (char *)addrstr.data();
    for (i=0; i<=n; i++)
        ptr[i] = coinaddr[i];
    CBitcoinAddress address(addrstr);
    if ( address.GetIndexKey(hashBytes, type) == 0 )
        return;
    addresses.push_back(std::make_pair(hashBytes,type));
    for (std::vector<std::pair<uint160, int> >::iterator it = addresses.begin(); it != addresses.end(); it++)
    {
        if ( GetAddressIndex((*it).first, (*it).second, addressIndex) == 0 )
            return;
    }
}

uint64_t CCutxovalue(char *coinaddr,uint256 utxotxid,int32_t utxovout)
{
    uint256 txid; std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;
    SetCCunspents(unspentOutputs,coinaddr);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++)
    {
        txid = it->first.txhash;
        if ( txid == utxotxid && utxovout == it->first.index )
            return(it->second.satoshis);
    }
    return(0);
}

int32_t CC_vinselect(int32_t *aboveip,int64_t *abovep,int32_t *belowip,int64_t *belowp,struct CC_utxo utxos[],int32_t numunspents,uint64_t value)
{
    int32_t i,abovei,belowi; int64_t above,below,gap,atx_value;
    abovei = belowi = -1;
    for (above=below=i=0; i<numunspents; i++)
    {
        if ( (atx_value= utxos[i].nValue) <= 0 )
            continue;
        if ( atx_value == value )
        {
            *aboveip = *belowip = i;
            *abovep = *belowp = 0;
            return(i);
        }
        else if ( atx_value > value )
        {
            gap = (atx_value - value);
            if ( above == 0 || gap < above )
            {
                above = gap;
                abovei = i;
            }
        }
        else
        {
            gap = (value - atx_value);
            if ( below == 0 || gap < below )
            {
                below = gap;
                belowi = i;
            }
        }
        //printf("value %.8f gap %.8f abovei.%d %.8f belowi.%d %.8f\n",dstr(value),dstr(gap),abovei,dstr(above),belowi,dstr(below));
    }
    *aboveip = abovei;
    *abovep = above;
    *belowip = belowi;
    *belowp = below;
    //printf("above.%d below.%d\n",abovei,belowi);
    if ( abovei >= 0 && belowi >= 0 )
    {
        if ( above < (below >> 1) )
            return(abovei);
        else return(belowi);
    }
    else if ( abovei >= 0 )
        return(abovei);
    else return(belowi);
    //return(abovei >= 0 && above < (below>>1) ? abovei : belowi);
}

uint64_t AddNormalinputs(CMutableTransaction &mtx,CPubKey mypk,uint64_t total,int32_t maxinputs)
{
    int32_t abovei,belowi,ind,vout,i,n = 0,maxutxos=1024; int64_t above,below; uint64_t remains,nValue,totalinputs = 0; uint256 txid,hashBlock; std::vector<COutput> vecOutputs; CTransaction tx; struct CC_utxo *utxos,*up;
#ifdef ENABLE_WALLET
    const CKeyStore& keystore = *pwalletMain;
    assert(pwalletMain != NULL);
    LOCK2(cs_main, pwalletMain->cs_wallet);
    pwalletMain->AvailableCoins(vecOutputs, false, NULL, true);
    utxos = (struct CC_utxo *)calloc(maxutxos,sizeof(*utxos));
    BOOST_FOREACH(const COutput& out, vecOutputs)
    {
        if ( out.fSpendable != 0 )
        {
            txid = out.tx->GetHash();
            vout = out.i;
            for (i=0; i<mtx.vin.size(); i++)
                if ( txid == mtx.vin[i].prevout.hash && vout == mtx.vin[i].prevout.n )
                    break;
            if ( i != mtx.vin.size() )
                continue;
            if ( myIsutxo_spentinmempool(txid,vout) == 0 )
            {
                up = &utxos[n++];
                up->txid = txid;
                up->nValue = out.tx->vout[out.i].nValue;
                up->vout = vout;
                /*mtx.vin.push_back(CTxIn(txid,vout,CScript()));
                nValue = out.tx->vout[out.i].nValue;
                totalinputs += nValue;
                n++;
                if ( totalinputs >= total || n >= maxinputs )
                    break;*/
                if ( n >= maxutxos )
                    break;
            }
        }
    }
    remains = total;
    for (i=0; i<maxinputs; i++)
    {
        below = above = 0;
        abovei = belowi = -1;
        if ( CC_vinselect(&abovei,&above,&belowi,&below,utxos,n,remains) < 0 )
        {
            printf("error finding unspent i.%d of %d, %.8f vs %.8f\n",i,n,(double)remains/COIN,(double)total/COIN);
            free(utxos);
            return(0);
        }
        if ( belowi < 0 || abovei >= 0 )
            ind = abovei;
        else ind = belowi;
        if ( ind < 0 )
        {
            printf("error finding unspent i.%d of %d, %.8f vs %.8f, abovei.%d belowi.%d ind.%d\n",i,n,(double)remains/COIN,(double)total/COIN,abovei,belowi,ind);
            free(utxos);
            return(0);
        }
        up = &utxos[ind];
        utxos[ind] = utxos[--n];
        memset(&utxos[n],0,sizeof(utxos[n]));
        mtx.vin.push_back(CTxIn(up->txid,up->vout,CScript()));
        totalinputs += up->nValue;
        remains -= up->nValue;
        if ( totalinputs >= total || (i+1) >= maxinputs )
            break;
    }
    free(utxos);
    if ( totalinputs >= total )
        return(totalinputs);
#endif
    return(0);
}
