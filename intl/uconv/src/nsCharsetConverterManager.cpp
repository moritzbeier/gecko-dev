/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: NPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Communicator client code.
 *
 * The Initial Developer of the Original Code is 
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Pierre Phaneuf <pp@ludusdesign.com>
 *
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the NPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the NPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "nsCOMPtr.h"
#include "nsString.h"
#include "nsReadableUtils.h"
#include "nsUnicharUtils.h"
#include "nsICharsetAlias.h"
#include "nsIServiceManager.h"
#include "nsICategoryManager.h"
#include "nsICharsetConverterManager.h"
#include "nsEncoderDecoderUtils.h"
#include "nsIStringBundle.h"
#include "nsILocaleService.h"
#include "nsUConvDll.h"
#include "prmem.h"
#include "nsCRT.h"
#include "nsVoidArray.h"
#include "nsStringEnumerator.h"

#include "nsXPCOM.h"
#include "nsISupportsPrimitives.h"
#include "nsICharsetDetector.h"

// just for CIDs
#include "nsIUnicodeDecodeHelper.h"
#include "nsIUnicodeEncodeHelper.h"
#include "nsCharsetConverterManager.h"
#include "nsNativeUConvService.h"

static NS_DEFINE_CID(kStringBundleServiceCID, NS_STRINGBUNDLESERVICE_CID);
static NS_DEFINE_CID(kCharsetAliasCID, NS_CHARSETALIAS_CID);

// Pattern of cached, commonly used, single byte decoder
#define NS_1BYTE_CODER_PATTERN "ISO-8859"
#define NS_1BYTE_CODER_PATTERN_LEN 8

// Class nsCharsetConverterManager [implementation]

NS_IMPL_THREADSAFE_ISUPPORTS1(nsCharsetConverterManager,
                              nsICharsetConverterManager)

nsCharsetConverterManager::nsCharsetConverterManager() 
  :mDataBundle(NULL), mTitleBundle(NULL)
{
#ifdef MOZ_USE_NATIVE_UCONV
  mNativeUC = do_GetService(NS_NATIVE_UCONV_SERVICE_CONTRACT_ID);
#endif
}

nsCharsetConverterManager::~nsCharsetConverterManager() 
{
  NS_IF_RELEASE(mDataBundle);
  NS_IF_RELEASE(mTitleBundle);
}


nsresult nsCharsetConverterManager::RegisterConverterManagerData()
{
  nsresult rv;
  nsCOMPtr<nsICategoryManager> catman = do_GetService(NS_CATEGORYMANAGER_CONTRACTID, &rv);
  if (NS_FAILED(rv)) return rv;

  RegisterConverterCategory(catman, NS_TITLE_BUNDLE_CATEGORY,
                            "chrome://global/locale/charsetTitles.properties");
  RegisterConverterCategory(catman, NS_DATA_BUNDLE_CATEGORY,
                            "resource://gre/res/charsetData.properties");

  return NS_OK;
}

nsresult
nsCharsetConverterManager::RegisterConverterCategory(nsICategoryManager* catman,
                                                     const char* aCategory,
                                                     const char* aURL)
{
  return catman->AddCategoryEntry(aCategory, aURL, "",
                                  PR_TRUE, PR_TRUE, nsnull);
}

nsresult nsCharsetConverterManager::LoadExtensibleBundle(
                                    const char* aCategory, 
                                    nsIStringBundle ** aResult)
{
  nsresult res = NS_OK;

  nsCOMPtr<nsIStringBundleService> sbServ = 
           do_GetService(kStringBundleServiceCID, &res);
  if (NS_FAILED(res)) return res;

  res = sbServ->CreateExtensibleBundle(aCategory, aResult);
  if (NS_FAILED(res)) return res;

  return res;
}

nsresult nsCharsetConverterManager::GetBundleValue(nsIStringBundle * aBundle, 
                                                   const char * aName, 
                                                   const nsAFlatString& aProp, 
                                                   PRUnichar ** aResult)
{
  nsresult res = NS_OK;

  nsAutoString key; key.AssignWithConversion(aName);

  ToLowerCase(key); // we lowercase the main comparison key
  if (!aProp.IsEmpty()) key.Append(aProp.get()); // yes, this param may be NULL

  res = aBundle->GetStringFromName(key.get(), aResult);
  return res;
}

nsresult nsCharsetConverterManager::GetBundleValue(nsIStringBundle * aBundle, 
                                                   const char * aName, 
                                                   const nsAFlatString& aProp, 
                                                   nsAString& aResult)
{
  nsresult res = NS_OK;

  nsXPIDLString value;
  res = GetBundleValue(aBundle, aName, aProp, getter_Copies(value));
  if (NS_FAILED(res)) return res;

  aResult = value;

  return NS_OK;
}


//----------------------------------------------------------------------------//----------------------------------------------------------------------------
// Interface nsICharsetConverterManager [implementation]

NS_IMETHODIMP
nsCharsetConverterManager::GetUnicodeEncoder(const char * aDest, 
                                             nsIUnicodeEncoder ** aResult)
{
  // resolve the charset first
  nsCAutoString charset;
  
  // fully qualify to possibly avoid vtable call
  nsCharsetConverterManager::GetCharsetAlias(aDest, charset);

  return nsCharsetConverterManager::GetUnicodeEncoderRaw(charset.get(),
                                                         aResult);
}


NS_IMETHODIMP
nsCharsetConverterManager::GetUnicodeEncoderRaw(const char * aDest, 
                                                nsIUnicodeEncoder ** aResult)
{
  *aResult= nsnull;
  nsCOMPtr<nsIUnicodeEncoder> encoder;

#ifdef MOZ_USE_NATIVE_UCONV
  if (mNativeUC) {
    nsCOMPtr<nsISupports> supports;
    mNativeUC->GetNativeConverter("UCS-2", 
                                  aDest,
                                  getter_AddRefs(supports));

    encoder = do_QueryInterface(supports);

    if (encoder) {
      NS_ADDREF(*aResult = encoder);
      return NS_OK;
    }
  }
#endif  
  nsresult res = NS_OK;

  nsCAutoString
    contractid(NS_LITERAL_CSTRING(NS_UNICODEENCODER_CONTRACTID_BASE) +
               nsDependentCString(aDest));

  // Always create an instance since encoders hold state.
  encoder = do_CreateInstance(contractid.get(), &res);

  if (NS_FAILED(res))
    res = NS_ERROR_UCONV_NOCONV;
  else
  {
    *aResult = encoder.get();
    NS_ADDREF(*aResult);
  }
  return res;
}

NS_IMETHODIMP
nsCharsetConverterManager::GetUnicodeDecoder(const char * aSrc, 
                                             nsIUnicodeDecoder ** aResult)
{
  // resolve the charset first
  nsCAutoString charset;
  
  // fully qualify to possibly avoid vtable call
  nsCharsetConverterManager::GetCharsetAlias(aSrc, charset);

  return nsCharsetConverterManager::GetUnicodeDecoderRaw(charset.get(),
                                                         aResult);
}

NS_IMETHODIMP
nsCharsetConverterManager::GetUnicodeDecoderRaw(const char * aSrc, 
                                                nsIUnicodeDecoder ** aResult)
{
  *aResult= nsnull;
  nsCOMPtr<nsIUnicodeDecoder> decoder;

#ifdef MOZ_USE_NATIVE_UCONV
  if (mNativeUC) {
    nsCOMPtr<nsISupports> supports;
    mNativeUC->GetNativeConverter(aSrc,
                                  "UCS-2", 
                                  getter_AddRefs(supports));
    
    decoder = do_QueryInterface(supports);

    if (decoder) {
      NS_ADDREF(*aResult = decoder);
      return NS_OK;
    }
  }
#endif
  nsresult res = NS_OK;;

  NS_NAMED_LITERAL_CSTRING(kUnicodeDecoderContractIDBase,
                           NS_UNICODEDECODER_CONTRACTID_BASE);

  nsCAutoString contractid(kUnicodeDecoderContractIDBase +
                           nsDependentCString(aSrc));

  if (!strncmp(aSrc,
               NS_1BYTE_CODER_PATTERN,
               NS_1BYTE_CODER_PATTERN_LEN))
  {
    // Single byte decoders dont hold state. Optimize by using a service.
    decoder = do_GetService(contractid.get(), &res);
  }
  else
  {
    decoder = do_CreateInstance(contractid.get(), &res);
  }
  if(NS_FAILED(res))
    res = NS_ERROR_UCONV_NOCONV;
  else
  {
    *aResult = decoder.get();
    NS_ADDREF(*aResult);
  }
  return res;
}

nsresult 
nsCharsetConverterManager::GetList(const nsACString& aCategory,
                                   const nsACString& aPrefix,
                                   nsIUTF8StringEnumerator** aResult)
{
  if (aResult == NULL) 
    return NS_ERROR_NULL_POINTER;
  *aResult = NULL;

  nsresult rv;
  nsCAutoString alias;

  nsCStringArray* array = new nsCStringArray;
  if (!array)
    return NS_ERROR_OUT_OF_MEMORY;

  nsCOMPtr<nsICategoryManager> catman = do_GetService(NS_CATEGORYMANAGER_CONTRACTID, &rv);
  if (NS_FAILED(rv)) return rv;
  
  nsCOMPtr<nsISimpleEnumerator> enumerator;
  catman->EnumerateCategory(PromiseFlatCString(aCategory).get(), 
                            getter_AddRefs(enumerator));

  PRBool hasMore;
  while (NS_SUCCEEDED(enumerator->HasMoreElements(&hasMore)) && hasMore) {
    nsCOMPtr<nsISupports> supports;
    if (NS_FAILED(enumerator->GetNext(getter_AddRefs(supports))))
      continue;
    
    nsCOMPtr<nsISupportsCString> supStr = do_QueryInterface(supports);
    if (!supStr)
      continue;

    nsCAutoString fullName(aPrefix);
    
    nsCAutoString name;
    if (NS_FAILED(supStr->GetData(name)))
      continue;

    fullName += name;
    rv = GetCharsetAlias(fullName.get(), alias);
    if (NS_FAILED(rv)) 
      continue;

    rv = array->AppendCString(alias);
  }
    
  return NS_NewAdoptingUTF8StringEnumerator(aResult, array);
}

// we should change the interface so that we can just pass back a enumerator!
NS_IMETHODIMP
nsCharsetConverterManager::GetDecoderList(nsIUTF8StringEnumerator ** aResult)
{
  return GetList(NS_LITERAL_CSTRING(NS_UNICODEDECODER_NAME),
                 EmptyCString(), aResult);
}

NS_IMETHODIMP
nsCharsetConverterManager::GetEncoderList(nsIUTF8StringEnumerator ** aResult)
{
  return GetList(NS_LITERAL_CSTRING(NS_UNICODEENCODER_NAME),
                 EmptyCString(), aResult);
}

NS_IMETHODIMP
nsCharsetConverterManager::GetCharsetDetectorList(nsIUTF8StringEnumerator** aResult)
{
  return GetList(NS_LITERAL_CSTRING(NS_CHARSET_DETECTOR_CATEGORY),
                 NS_LITERAL_CSTRING("chardet."), aResult);
}

// XXX Improve the implementation of this method. Right now, it is build on 
// top of the nsCharsetAlias service. We can make the nsCharsetAlias
// better, with its own hash table (not the StringBundle anymore) and
// a nicer file format.
NS_IMETHODIMP
nsCharsetConverterManager::GetCharsetAlias(const char * aCharset, 
                                           nsACString& aResult)
{
  NS_PRECONDITION(aCharset, "null param");
  if (!aCharset)
    return NS_ERROR_NULL_POINTER;

  // We try to obtain the preferred name for this charset from the charset 
  // aliases. If we don't get it from there, we just use the original string
  nsDependentCString charset(aCharset);
  nsCOMPtr<nsICharsetAlias> csAlias( do_GetService(kCharsetAliasCID) );
  NS_ASSERTION(csAlias, "failed to get the CharsetAlias service");
  if (csAlias) {
    nsAutoString pref;
    nsresult res = csAlias->GetPreferred(charset, aResult);
    if (NS_SUCCEEDED(res)) {
      return (!aResult.IsEmpty()) ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
    }
  }

  aResult = charset;
  return NS_OK;
}


NS_IMETHODIMP
nsCharsetConverterManager::GetCharsetTitle(const char * aCharset, 
                                           nsAString& aResult)
{
  if (aCharset == NULL) return NS_ERROR_NULL_POINTER;

  nsresult res = NS_OK;

  if (mTitleBundle == NULL) {
    res = LoadExtensibleBundle(NS_TITLE_BUNDLE_CATEGORY, &mTitleBundle);
    if (NS_FAILED(res)) return res;
  }

  res = GetBundleValue(mTitleBundle, aCharset, NS_LITERAL_STRING(".title"), aResult);
  return res;
}

NS_IMETHODIMP
nsCharsetConverterManager::GetCharsetData(const char * aCharset, 
                                          const PRUnichar * aProp,
                                          nsAString& aResult)
{
  if (aCharset == NULL) return NS_ERROR_NULL_POINTER;
  // aProp can be NULL

  nsresult res = NS_OK;

  if (mDataBundle == NULL) {
    res = LoadExtensibleBundle(NS_DATA_BUNDLE_CATEGORY, &mDataBundle);
    if (NS_FAILED(res)) return res;
  }

  res = GetBundleValue(mDataBundle, aCharset, nsDependentString(aProp), aResult);
  return res;
}

NS_IMETHODIMP
nsCharsetConverterManager::GetCharsetLangGroup(const char * aCharset, 
                                               nsIAtom** aResult)
{
  // resolve the charset first
  nsCAutoString charset;

  nsresult rv = GetCharsetAlias(aCharset, charset);
  if (NS_FAILED(rv)) return rv;

  // fully qualify to possibly avoid vtable call
  return nsCharsetConverterManager::GetCharsetLangGroupRaw(charset.get(),
                                                           aResult);
}

NS_IMETHODIMP
nsCharsetConverterManager::GetCharsetLangGroupRaw(const char * aCharset, 
                                                  nsIAtom** aResult)
{

  if (aCharset == NULL) return NS_ERROR_NULL_POINTER;

  nsresult res = NS_OK;

  if (mDataBundle == NULL) {
    res = LoadExtensibleBundle(NS_DATA_BUNDLE_CATEGORY, &mDataBundle);
    if (NS_FAILED(res)) return res;
  }

  nsAutoString langGroup;
  res = GetBundleValue(mDataBundle, aCharset, NS_LITERAL_STRING(".LangGroup"), langGroup);

  *aResult = NS_NewAtom(langGroup);
  return res;
}
