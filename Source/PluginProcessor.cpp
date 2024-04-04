/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
KPdelayAudioProcessor::KPdelayAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                      )
#endif
{
    addParameter(mDryWetParameter = new juce::AudioParameterFloat("drywet",
                                                                  "Dry Wet",
                                                                  0,
                                                                  1.0,
                                                                  0.5));
    
    addParameter(mFeedbackParameter = new juce::AudioParameterFloat("feedback",
                                                                     "Feedback",
                                                                     0,
                                                                     0.98,
                                                                     0.5));
    
    addParameter(mTimeParameter = new juce::AudioParameterFloat("delaytime",
                                                                     "Delay time",
                                                                     0.01,
                                                                     MAX_DELAY_TIME,
                                                                     1.0));
    
    mTimeSmoothed = 0;
    mCircularBufferLeft = nullptr;
    mCircularBufferRight = nullptr;
    mCircularBufferWriteHead = 0;
    mCircularBufferLength = 0;
    mDelayTimeInSamples = 0;
    mDelayReadHead = 0;
    
    mFeedbackLeft = 0;
    mFeedbackRight = 0;
    
    mDryWet = 0.5;
    
    
    


}

KPdelayAudioProcessor::~KPdelayAudioProcessor()
{
    if (mCircularBufferLeft !=nullptr){
        delete [] mCircularBufferLeft;
        mCircularBufferLeft = nullptr;
    }
    if (mCircularBufferRight !=nullptr){
        delete [] mCircularBufferRight;
        mCircularBufferRight = nullptr;
    }
}

//==============================================================================
const juce::String KPdelayAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool KPdelayAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool KPdelayAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool KPdelayAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double KPdelayAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int KPdelayAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int KPdelayAudioProcessor::getCurrentProgram()
{
    return 0;
}

void KPdelayAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String KPdelayAudioProcessor::getProgramName (int index)
{
    return {};
}

void KPdelayAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void KPdelayAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    mDelayTimeInSamples = sampleRate * *mTimeParameter;
    
    mCircularBufferLength = sampleRate * MAX_DELAY_TIME;

    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    if (mCircularBufferLeft != nullptr ) {
       delete [] mCircularBufferLeft;
       mCircularBufferLeft = nullptr;
    }
    if (mCircularBufferLeft ==nullptr){
        mCircularBufferLeft = new float [(int)mCircularBufferLength];
    }
    
    juce::zeromem(mCircularBufferLeft,mCircularBufferLength * sizeof(float));
    
    
    if (mCircularBufferRight != nullptr ) {
       delete [] mCircularBufferRight;
       mCircularBufferRight = nullptr;
    }
    if (mCircularBufferRight ==nullptr){
        mCircularBufferRight = new float [(int)mCircularBufferLength];
    }
    
    juce::zeromem(mCircularBufferRight,mCircularBufferLength * sizeof(float));

    
    mCircularBufferWriteHead = 0;
    
    mTimeSmoothed = *mTimeParameter;

    
}

void KPdelayAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

//#ifndef JucePlugin_PreferredChannelConfigurations
bool KPdelayAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  //#if JucePlugin_IsMidiEffect
  //  juce::ignoreUnused (layouts);
  //  return true;
  //#else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    /*if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    */
    if (layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo() &&
        layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()) {
        return true;
    } else {
        return false;
    }
    
    // This checks if the input layout matches the output layout
   /*#if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   //#endif

    return true;
  //#endif */
}
//#endif

void KPdelayAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());
    
    
        
    float* leftchannel = buffer.getWritePointer(0);
    float* rightchannel = buffer.getWritePointer(1);
    
    for (int i =0; i < buffer.getNumSamples(); i++){
        
        mTimeSmoothed = mTimeSmoothed - 0.0001 * (mTimeSmoothed - *mTimeParameter);
        mDelayTimeInSamples = getSampleRate() * mTimeSmoothed;
        
        mCircularBufferLeft[mCircularBufferWriteHead] = leftchannel[i] + mFeedbackLeft;
        mCircularBufferRight[mCircularBufferWriteHead] = rightchannel[i] + mFeedbackRight;
        
        mDelayReadHead = mCircularBufferWriteHead - mDelayTimeInSamples;
        
        if (mDelayReadHead < 0){
            mDelayReadHead += mCircularBufferLength;
        }
        
        int readHead_x = (int)mDelayReadHead;
        int readHead_x1 = readHead_x + 1;
        float readHeadFloat = mDelayReadHead - readHead_x;
        
        if (readHead_x1 >= mCircularBufferLength){
            readHead_x1 -= mCircularBufferLength;
        }
        
        
        float delay_sample_left = KPdelayAudioProcessor::lin_interp(mCircularBufferLeft[readHead_x], mCircularBufferLeft[readHead_x1], readHeadFloat);
        float delay_sample_right = KPdelayAudioProcessor::lin_interp(mCircularBufferRight[readHead_x], mCircularBufferRight[readHead_x1], readHeadFloat);
        

        
        mFeedbackLeft = delay_sample_left * *mFeedbackParameter;
        mFeedbackRight = delay_sample_right * *mFeedbackParameter;
        
        mCircularBufferWriteHead++;
        
        buffer.setSample(0, i, buffer.getSample(0, i) * (1 - *mDryWetParameter) + delay_sample_left * *mDryWetParameter);
        buffer.setSample(1, i, buffer.getSample(1, i) * (1 - *mDryWetParameter) + delay_sample_right * *mDryWetParameter);
        
        //buffer.setSample(0, i, buffer.getSample(0, i) * (1 - 0.5) + delay_sample_left * 0.5);
        //buffer.setSample(1, i, buffer.getSample(1, i) * (1 - 0.5) + delay_sample_right * 0.5);
        
        
        if (mCircularBufferWriteHead >= mCircularBufferLength){
            mCircularBufferWriteHead = 0;
        }
    }

    

    // This is the place where you'd normally do the guts of your plugin's
    // audio processing...
    // Make sure to reset the state if your inner loop is processing
    // the samples and the outer loop is handling the channels.
    // Alternatively, you can process the samples with the channels
    // interleaved by keeping the same state.
    //for (int channel = 0; channel < totalNumInputChannels; ++channel)
    //{
    //    auto* channelData = buffer.getWritePointer (channel);

        // ..do something to the data...
    //}
}

//==============================================================================
bool KPdelayAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* KPdelayAudioProcessor::createEditor()
{
    return new KPdelayAudioProcessorEditor (*this);
}

//==============================================================================
void KPdelayAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void KPdelayAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KPdelayAudioProcessor();
}


float KPdelayAudioProcessor::lin_interp(float inSampleX, float inSampleY, float inFloatPhase)
{
    return (1-inFloatPhase) * inSampleX + inFloatPhase * inSampleY;
}
